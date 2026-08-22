#include "wasapi-renderer.hpp"

#include "audio-capture.hpp"

#include <obs-module.h>

#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kTargetDeviceName[] = L"CABLE Input";
constexpr PROPERTYKEY kDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string utf8_from_wide(const wchar_t *value)
{
    if (!value)
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::wstring wide_from_utf8(const std::string &value)
{
    if (value.empty())
        return {};

    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (size <= 1)
        return {};

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}

bool contains_case_insensitive(const std::wstring &value, const std::wstring &needle)
{
    std::wstring folded_value(value.size(), L'\0');
    std::wstring folded_needle(needle.size(), L'\0');

    std::transform(value.begin(), value.end(), folded_value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    std::transform(needle.begin(), needle.end(), folded_needle.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

    return folded_value.find(folded_needle) != std::wstring::npos;
}

void log_hresult(const char *operation, HRESULT result)
{
    blog(LOG_ERROR, "[obs-virtual-audio] %s failed: HRESULT 0x%08lX", operation,
         static_cast<unsigned long>(result));
}

HRESULT get_device_details(IMMDevice *device, WasapiDevice &details)
{
    LPWSTR device_id = nullptr;
    HRESULT result = device->GetId(&device_id);
    if (FAILED(result))
        return result;

    details.id = utf8_from_wide(device_id);
    CoTaskMemFree(device_id);

    ComPtr<IPropertyStore> properties;
    result = device->OpenPropertyStore(STGM_READ, &properties);
    if (FAILED(result))
        return result;

    PROPVARIANT name;
    PropVariantInit(&name);
    result = properties->GetValue(kDeviceFriendlyName, &name);
    if (SUCCEEDED(result) && name.vt == VT_LPWSTR && name.pwszVal)
        details.name = utf8_from_wide(name.pwszVal);
    else if (SUCCEEDED(result))
        result = E_UNEXPECTED;
    PropVariantClear(&name);
    return result;
}

HRESULT find_render_device(IMMDeviceEnumerator *enumerator, const std::string &target_device_id,
                           ComPtr<IMMDevice> &device, WasapiDevice &details)
{
    if (!target_device_id.empty()) {
        const std::wstring wide_id = wide_from_utf8(target_device_id);
        if (wide_id.empty())
            return E_INVALIDARG;

        HRESULT result = enumerator->GetDevice(wide_id.c_str(), &device);
        if (FAILED(result))
            return result;
        return get_device_details(device.Get(), details);
    }

    ComPtr<IMMDeviceCollection> devices;
    HRESULT result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(result))
        return result;

    UINT count = 0;
    result = devices->GetCount(&count);
    if (FAILED(result))
        return result;

    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> candidate;
        result = devices->Item(index, &candidate);
        if (FAILED(result))
            continue;

        WasapiDevice candidate_details;
        result = get_device_details(candidate.Get(), candidate_details);
        if (SUCCEEDED(result) &&
            contains_case_insensitive(wide_from_utf8(candidate_details.name), kTargetDeviceName)) {
            details = std::move(candidate_details);
            device = std::move(candidate);
            return S_OK;
        }
    }

    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
} // namespace

WasapiRenderer::WasapiRenderer(AudioCapture &capture, std::string target_device_id)
    : capture_(capture), target_device_id_(std::move(target_device_id))
{
}

WasapiRenderer::~WasapiRenderer() { stop(); }

bool WasapiRenderer::start()
{
    if (running_.load(std::memory_order_acquire))
        return true;
    if (thread_.joinable())
        stop();

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    audio_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop_event_ || !audio_event_) {
        blog(LOG_ERROR, "[obs-virtual-audio] failed to create WASAPI synchronization events");
        stop();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(initialization_mutex_);
        initialization_complete_ = false;
        initialization_succeeded_ = false;
    }

    rendered_frames_.store(0, std::memory_order_release);
    underrun_frames_.store(0, std::memory_order_release);
    state_.store(WasapiRendererState::connecting, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&WasapiRenderer::run, this);

    std::unique_lock<std::mutex> lock(initialization_mutex_);
    initialization_cv_.wait(lock, [this] { return initialization_complete_; });
    const bool succeeded = initialization_succeeded_;
    lock.unlock();

    if (!succeeded)
        stop();

    return succeeded;
}

void WasapiRenderer::stop()
{
    if (stop_event_)
        SetEvent(static_cast<HANDLE>(stop_event_));

    if (thread_.joinable())
        thread_.join();

    running_.store(false, std::memory_order_release);
    state_.store(WasapiRendererState::stopped, std::memory_order_release);

    if (audio_event_) {
        CloseHandle(static_cast<HANDLE>(audio_event_));
        audio_event_ = nullptr;
    }
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
}

bool WasapiRenderer::running() const noexcept { return running_.load(std::memory_order_acquire); }

WasapiRendererState WasapiRenderer::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

uint64_t WasapiRenderer::rendered_frames() const noexcept
{
    return rendered_frames_.load(std::memory_order_acquire);
}

uint64_t WasapiRenderer::underrun_frames() const noexcept
{
    return underrun_frames_.load(std::memory_order_acquire);
}

const std::string &WasapiRenderer::target_device_id() const noexcept { return target_device_id_; }

std::string WasapiRenderer::active_device_id() const
{
    std::lock_guard<std::mutex> lock(device_mutex_);
    return active_device_id_;
}

std::string WasapiRenderer::active_device_name() const
{
    std::lock_guard<std::mutex> lock(device_mutex_);
    return active_device_name_;
}

std::vector<WasapiDevice> WasapiRenderer::enumerate_devices()
{
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool owns_com = SUCCEEDED(com_result);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
        log_hresult("CoInitializeEx while enumerating devices", com_result);
        return {};
    }

    std::vector<WasapiDevice> result_devices;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(result)) {
        ComPtr<IMMDeviceCollection> devices;
        result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        UINT count = 0;
        if (SUCCEEDED(result))
            result = devices->GetCount(&count);

        for (UINT index = 0; SUCCEEDED(result) && index < count; ++index) {
            ComPtr<IMMDevice> device;
            if (FAILED(devices->Item(index, &device)))
                continue;

            WasapiDevice details;
            if (SUCCEEDED(get_device_details(device.Get(), details)))
                result_devices.push_back(std::move(details));
        }
    }

    if (FAILED(result))
        log_hresult("enumerating playback devices", result);
    if (owns_com)
        CoUninitialize();

    std::sort(
        result_devices.begin(), result_devices.end(),
        [](const WasapiDevice &left, const WasapiDevice &right) { return left.name < right.name; });
    return result_devices;
}

void WasapiRenderer::report_initialization(bool succeeded) noexcept
{
    {
        std::lock_guard<std::mutex> lock(initialization_mutex_);
        initialization_succeeded_ = succeeded;
        initialization_complete_ = true;
    }
    initialization_cv_.notify_one();
}

void WasapiRenderer::run() noexcept
{
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result)) {
        log_hresult("CoInitializeEx", com_result);
        report_initialization(false);
        running_.store(false, std::memory_order_release);
        return;
    }

    report_initialization(true);

    std::string retry_device_id = target_device_id_;
    DWORD retry_delay_ms = 1000;
    bool first_attempt = true;

    while (WaitForSingleObject(static_cast<HANDLE>(stop_event_), 0) != WAIT_OBJECT_0) {
        state_.store(first_attempt ? WasapiRendererState::connecting
                                   : WasapiRendererState::reconnecting,
                     std::memory_order_release);

        ComPtr<IAudioClient> audio_client;
        const auto connect_and_render = [&]() -> bool {
            ComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                              IID_PPV_ARGS(&enumerator));
            if (FAILED(result)) {
                log_hresult("creating the audio device enumerator", result);
                return false;
            }

            ComPtr<IMMDevice> device;
            WasapiDevice device_details;
            result = find_render_device(enumerator.Get(), retry_device_id, device, device_details);
            if (FAILED(result)) {
                if (retry_device_id.empty())
                    blog(LOG_ERROR,
                         "[obs-virtual-audio] no active playback device containing '%ls' was found",
                         kTargetDeviceName);
                else
                    blog(LOG_ERROR,
                         "[obs-virtual-audio] configured playback device is unavailable");
                return false;
            }

            result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void **>(audio_client.GetAddressOf()));
            if (FAILED(result)) {
                log_hresult("activating the WASAPI audio client", result);
                return false;
            }

            WAVEFORMATEXTENSIBLE format{};
            format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            format.Format.nChannels = static_cast<WORD>(AudioCapture::kChannels);
            format.Format.nSamplesPerSec = AudioCapture::kSampleRate;
            format.Format.wBitsPerSample = 32;
            format.Format.nBlockAlign = format.Format.nChannels * format.Format.wBitsPerSample / 8;
            format.Format.nAvgBytesPerSec =
                format.Format.nSamplesPerSec * format.Format.nBlockAlign;
            format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
            format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

            constexpr DWORD stream_flags =
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            result = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, 0, 0,
                                              &format.Format, nullptr);
            if (FAILED(result)) {
                log_hresult("initializing the WASAPI render stream", result);
                return false;
            }

            result = audio_client->SetEventHandle(static_cast<HANDLE>(audio_event_));
            if (FAILED(result)) {
                log_hresult("setting the WASAPI render event", result);
                return false;
            }

            UINT32 buffer_frames = 0;
            result = audio_client->GetBufferSize(&buffer_frames);
            if (FAILED(result)) {
                log_hresult("querying the WASAPI buffer size", result);
                return false;
            }

            ComPtr<IAudioRenderClient> render_client;
            result = audio_client->GetService(IID_PPV_ARGS(&render_client));
            if (FAILED(result)) {
                log_hresult("creating the WASAPI render client", result);
                return false;
            }

            BYTE *initial_buffer = nullptr;
            result = render_client->GetBuffer(buffer_frames, &initial_buffer);
            if (FAILED(result)) {
                log_hresult("acquiring the initial WASAPI buffer", result);
                return false;
            }
            result = render_client->ReleaseBuffer(buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
            if (FAILED(result)) {
                log_hresult("releasing the initial WASAPI buffer", result);
                return false;
            }

            capture_.discard_buffered_audio();
            result = audio_client->Start();
            if (FAILED(result)) {
                log_hresult("starting the WASAPI render stream", result);
                return false;
            }

            if (retry_device_id.empty())
                retry_device_id = device_details.id;
            {
                std::lock_guard<std::mutex> lock(device_mutex_);
                active_device_id_ = device_details.id;
                active_device_name_ = device_details.name;
            }
            state_.store(WasapiRendererState::connected, std::memory_order_release);
            blog(LOG_INFO, "[obs-virtual-audio] WASAPI output started: %s, %u Hz, stereo float",
                 device_details.name.c_str(), AudioCapture::kSampleRate);

            HANDLE events[] = {static_cast<HANDLE>(stop_event_), static_cast<HANDLE>(audio_event_)};
            while (true) {
                const DWORD wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait_result == WAIT_OBJECT_0)
                    break;
                if (wait_result != WAIT_OBJECT_0 + 1) {
                    blog(LOG_ERROR, "[obs-virtual-audio] waiting for WASAPI failed: error %lu",
                         GetLastError());
                    break;
                }

                UINT32 padding = 0;
                result = audio_client->GetCurrentPadding(&padding);
                if (FAILED(result)) {
                    log_hresult("querying WASAPI buffer padding", result);
                    break;
                }

                const UINT32 available_frames = buffer_frames - padding;
                if (available_frames == 0)
                    continue;

                BYTE *output = nullptr;
                result = render_client->GetBuffer(available_frames, &output);
                if (FAILED(result)) {
                    log_hresult("acquiring the WASAPI render buffer", result);
                    break;
                }

                const size_t requested_samples =
                    static_cast<size_t>(available_frames) * AudioCapture::kChannels;
                auto *samples = reinterpret_cast<float *>(output);
                const size_t read_samples = capture_.read(samples, requested_samples);
                std::fill(samples + read_samples, samples + requested_samples, 0.0f);

                const uint64_t read_frames = read_samples / AudioCapture::kChannels;
                rendered_frames_.fetch_add(read_frames, std::memory_order_relaxed);
                underrun_frames_.fetch_add(available_frames - read_frames,
                                           std::memory_order_relaxed);

                result = render_client->ReleaseBuffer(available_frames, 0);
                if (FAILED(result)) {
                    log_hresult("releasing the WASAPI render buffer", result);
                    break;
                }
            }

            audio_client->Stop();
            return true;
        };

        const bool connected_this_attempt = connect_and_render();
        if (WaitForSingleObject(static_cast<HANDLE>(stop_event_), 0) == WAIT_OBJECT_0)
            break;

        state_.store(WasapiRendererState::reconnecting, std::memory_order_release);
        if (connected_this_attempt)
            retry_delay_ms = 1000;

        blog(LOG_WARNING, "[obs-virtual-audio] WASAPI output unavailable; reconnecting in %lu ms",
             retry_delay_ms);
        const DWORD wait_result =
            WaitForSingleObject(static_cast<HANDLE>(stop_event_), retry_delay_ms);
        if (wait_result == WAIT_OBJECT_0)
            break;
        if (wait_result == WAIT_FAILED) {
            blog(LOG_ERROR, "[obs-virtual-audio] reconnect wait failed: error %lu", GetLastError());
            break;
        }

        if (!connected_this_attempt)
            retry_delay_ms = std::min<DWORD>(retry_delay_ms * 2, 10000);
        first_attempt = false;
    }

    state_.store(WasapiRendererState::stopped, std::memory_order_release);

    blog(LOG_INFO,
         "[obs-virtual-audio] WASAPI output stopped: rendered=%llu frames, underrun=%llu frames",
         static_cast<unsigned long long>(rendered_frames()),
         static_cast<unsigned long long>(underrun_frames()));
    running_.store(false, std::memory_order_release);
    CoUninitialize();
}
