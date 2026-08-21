#pragma once

#include "audio-ring-buffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

struct audio_data;

class AudioCapture {
public:
    static constexpr uint32_t kSampleRate = 48000;
    static constexpr size_t kChannels = 2;
    static constexpr size_t kMixIndex = 0;
    static constexpr size_t kMaxBufferedSamples = kSampleRate * kChannels / 20; // 50 ms

    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture &) = delete;
    AudioCapture &operator=(const AudioCapture &) = delete;

    bool start();
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] size_t buffered_samples() const noexcept;
    [[nodiscard]] uint64_t dropped_samples() const noexcept;

    size_t read(float *samples, size_t sample_count) noexcept;
    void discard_buffered_audio() noexcept;

private:
    static void raw_audio_callback(void *param, size_t mix_idx, audio_data *data);
    void on_audio(size_t mix_idx, audio_data *data) noexcept;

    // Two seconds of stereo float audio at 48 kHz.
    AudioRingBuffer ring_buffer_{kSampleRate * kChannels * 2};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> dropped_samples_{0};
};
