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

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kTargetDeviceName[] = L"CABLE Input";
constexpr PROPERTYKEY kDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14};

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

HRESULT find_cable_input(IMMDeviceEnumerator *enumerator, ComPtr<IMMDevice> &device,
                         std::string &friendly_name)
{
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

        ComPtr<IPropertyStore> properties;
        result = candidate->OpenPropertyStore(STGM_READ, &properties);
        if (FAILED(result))
            continue;

        PROPVARIANT name;
        PropVariantInit(&name);
        result = properties->GetValue(kDeviceFriendlyName, &name);
        if (SUCCEEDED(result) && name.vt == VT_LPWSTR && name.pwszVal &&
            contains_case_insensitive(name.pwszVal, kTargetDeviceName)) {
            friendly_name = utf8_from_wide(name.pwszVal);
            device = std::move(candidate);
            PropVariantClear(&name);
            return S_OK;
        }

        PropVariantClear(&name);
    }

    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
} // namespace

WasapiRenderer::WasapiRenderer(AudioCapture &capture) : capture_(capture) {}

WasapiRenderer::~WasapiRenderer()
{
    stop();
}

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

    if (audio_event_) {
        CloseHandle(static_cast<HANDLE>(audio_event_));
        audio_event_ = nullptr;
    }
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
}

bool WasapiRenderer::running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

uint64_t WasapiRenderer::rendered_frames() const noexcept
{
    return rendered_frames_.load(std::memory_order_acquire);
}

uint64_t WasapiRenderer::underrun_frames() const noexcept
{
    return underrun_frames_.load(std::memory_order_acquire);
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

    bool initialization_reported = false;
    ComPtr<IAudioClient> audio_client;

    const auto initialize_and_render = [&]() -> bool {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(&enumerator));
        if (FAILED(result)) {
            log_hresult("creating the audio device enumerator", result);
            return false;
        }

        ComPtr<IMMDevice> device;
        std::string friendly_name;
        result = find_cable_input(enumerator.Get(), device, friendly_name);
        if (FAILED(result)) {
            blog(LOG_ERROR,
                 "[obs-virtual-audio] no active playback device containing '%ls' was found",
                 kTargetDeviceName);
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
        format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
        format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        constexpr DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                       AUDCLNT_STREAMFLAGS_NOPERSIST |
                                       AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                       AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
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

        result = audio_client->Start();
        if (FAILED(result)) {
            log_hresult("starting the WASAPI render stream", result);
            return false;
        }

        blog(LOG_INFO, "[obs-virtual-audio] WASAPI output started: %s, %u Hz, stereo float",
             friendly_name.c_str(), AudioCapture::kSampleRate);
        report_initialization(true);
        initialization_reported = true;

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
            underrun_frames_.fetch_add(available_frames - read_frames, std::memory_order_relaxed);

            result = render_client->ReleaseBuffer(available_frames, 0);
            if (FAILED(result)) {
                log_hresult("releasing the WASAPI render buffer", result);
                break;
            }
        }

        audio_client->Stop();
        return true;
    };

    initialize_and_render();

    if (!initialization_reported)
        report_initialization(false);

    blog(LOG_INFO,
         "[obs-virtual-audio] WASAPI output stopped: rendered=%llu frames, underrun=%llu frames",
         static_cast<unsigned long long>(rendered_frames()),
         static_cast<unsigned long long>(underrun_frames()));
    running_.store(false, std::memory_order_release);
    CoUninitialize();
}
