#include "audio-ring-buffer.hpp"

#include <algorithm>

AudioRingBuffer::AudioRingBuffer(size_t capacity_samples)
    : buffer_(capacity_samples + 1)
{
}

size_t AudioRingBuffer::capacity() const noexcept
{
    return buffer_.empty() ? 0 : buffer_.size() - 1;
}

size_t AudioRingBuffer::available() const noexcept
{
    const size_t read = read_pos_.load(std::memory_order_acquire);
    const size_t write = write_pos_.load(std::memory_order_acquire);

    if (write >= read)
        return write - read;

    return buffer_.size() - read + write;
}

size_t AudioRingBuffer::write(const float *samples, size_t sample_count) noexcept
{
    if (!samples || sample_count == 0 || buffer_.size() < 2)
        return 0;

    const size_t read = read_pos_.load(std::memory_order_acquire);
    size_t write = write_pos_.load(std::memory_order_relaxed);

    const size_t used = write >= read ? write - read : buffer_.size() - read + write;
    const size_t free_samples = capacity() - used;
    const size_t to_write = std::min(sample_count, free_samples);

    for (size_t i = 0; i < to_write; ++i) {
        buffer_[write] = samples[i];
        write = (write + 1) % buffer_.size();
    }

    write_pos_.store(write, std::memory_order_release);
    return to_write;
}

size_t AudioRingBuffer::read(float *samples, size_t sample_count) noexcept
{
    if (!samples || sample_count == 0 || buffer_.size() < 2)
        return 0;

    size_t read = read_pos_.load(std::memory_order_relaxed);
    const size_t write = write_pos_.load(std::memory_order_acquire);

    const size_t used = write >= read ? write - read : buffer_.size() - read + write;
    const size_t to_read = std::min(sample_count, used);

    for (size_t i = 0; i < to_read; ++i) {
        samples[i] = buffer_[read];
        read = (read + 1) % buffer_.size();
    }

    read_pos_.store(read, std::memory_order_release);
    return to_read;
}

void AudioRingBuffer::clear() noexcept
{
    const size_t write = write_pos_.load(std::memory_order_acquire);
    read_pos_.store(write, std::memory_order_release);
}
