#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacity_samples);

    size_t write(const float *samples, size_t sample_count) noexcept;
    size_t read(float *samples, size_t sample_count) noexcept;

    [[nodiscard]] size_t available() const noexcept;
    [[nodiscard]] size_t capacity() const noexcept;
    size_t trim_to(size_t max_samples) noexcept;
    void clear() noexcept;

private:
    std::vector<float> buffer_;
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
};
