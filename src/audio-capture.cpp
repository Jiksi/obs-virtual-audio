#include "audio-capture.hpp"

#include <obs-module.h>
#include <media-io/audio-io.h>

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture()
{
    stop();
}

bool AudioCapture::start()
{
    if (running_.exchange(true, std::memory_order_acq_rel))
        return true;

    ring_buffer_.clear();
    dropped_samples_.store(0, std::memory_order_release);

    audio_convert_info conversion{};
    conversion.samples_per_sec = kSampleRate;
    conversion.format = AUDIO_FORMAT_FLOAT;
    conversion.speakers = SPEAKERS_STEREO;

    obs_add_raw_audio_callback(kMixIndex, &conversion, raw_audio_callback, this);

    blog(LOG_INFO,
         "[obs-virtual-audio] audio capture started: mix=%zu, %u Hz, stereo float",
         kMixIndex, kSampleRate);
    return true;
}

void AudioCapture::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    obs_remove_raw_audio_callback(kMixIndex, raw_audio_callback, this);
    ring_buffer_.clear();

    blog(LOG_INFO, "[obs-virtual-audio] audio capture stopped");
}

bool AudioCapture::running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

size_t AudioCapture::buffered_samples() const noexcept
{
    return ring_buffer_.available();
}

uint64_t AudioCapture::dropped_samples() const noexcept
{
    return dropped_samples_.load(std::memory_order_acquire);
}

size_t AudioCapture::read(float *samples, size_t sample_count) noexcept
{
    return ring_buffer_.read(samples, sample_count);
}

void AudioCapture::discard_buffered_audio() noexcept
{
    ring_buffer_.clear();
}

void AudioCapture::raw_audio_callback(void *param, size_t mix_idx, audio_data *data)
{
    if (!param)
        return;

    static_cast<AudioCapture *>(param)->on_audio(mix_idx, data);
}

void AudioCapture::on_audio(size_t mix_idx, audio_data *data) noexcept
{
    if (!running_.load(std::memory_order_relaxed) || !data || mix_idx != kMixIndex || !data->data[0])
        return;

    const size_t sample_count = static_cast<size_t>(data->frames) * kChannels;
    const auto *samples = reinterpret_cast<const float *>(data->data[0]);
    const size_t written = ring_buffer_.write(samples, sample_count);

    if (written < sample_count)
        dropped_samples_.fetch_add(sample_count - written, std::memory_order_relaxed);
}
