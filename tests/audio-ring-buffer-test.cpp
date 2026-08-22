#include "audio-ring-buffer.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';

    return condition;
}

bool test_empty_and_invalid_operations()
{
    AudioRingBuffer buffer(0);
    float sample = 1.0F;

    return expect(buffer.capacity() == 0, "zero-sized buffer reports zero capacity") &&
           expect(buffer.available() == 0, "zero-sized buffer starts empty") &&
           expect(buffer.write(&sample, 1) == 0, "zero-sized buffer rejects writes") &&
           expect(buffer.read(&sample, 1) == 0, "zero-sized buffer rejects reads") &&
           expect(buffer.write(nullptr, 1) == 0, "null write source is rejected") &&
           expect(buffer.read(nullptr, 1) == 0, "null read destination is rejected");
}

bool test_fifo_and_capacity_limit()
{
    AudioRingBuffer buffer(3);
    const std::array<float, 5> input{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    std::array<float, 5> output{};

    return expect(buffer.capacity() == 3, "configured capacity is preserved") &&
           expect(buffer.write(input.data(), input.size()) == 3, "writes stop at capacity") &&
           expect(buffer.available() == 3, "available count reaches capacity") &&
           expect(buffer.read(output.data(), output.size()) == 3, "reads only available samples") &&
           expect(output[0] == 1.0F && output[1] == 2.0F && output[2] == 3.0F,
                  "samples preserve FIFO order") &&
           expect(buffer.available() == 0, "buffer is empty after complete read");
}

bool test_wraparound()
{
    AudioRingBuffer buffer(4);
    const std::array<float, 3> first{1.0F, 2.0F, 3.0F};
    const std::array<float, 3> second{4.0F, 5.0F, 6.0F};
    std::array<float, 2> prefix{};
    std::array<float, 4> output{};

    return expect(buffer.write(first.data(), first.size()) == first.size(), "initial write succeeds") &&
           expect(buffer.read(prefix.data(), prefix.size()) == prefix.size(), "prefix read succeeds") &&
           expect(prefix[0] == 1.0F && prefix[1] == 2.0F, "prefix order is correct") &&
           expect(buffer.write(second.data(), second.size()) == second.size(), "wrapped write succeeds") &&
           expect(buffer.available() == output.size(), "wrapped buffer reports full capacity") &&
           expect(buffer.read(output.data(), output.size()) == output.size(), "wrapped read succeeds") &&
           expect(output[0] == 3.0F && output[1] == 4.0F && output[2] == 5.0F && output[3] == 6.0F,
                  "wraparound preserves FIFO order");
}

bool test_clear()
{
    AudioRingBuffer buffer(4);
    const std::array<float, 3> discarded{1.0F, 2.0F, 3.0F};
    const std::array<float, 2> replacement{8.0F, 9.0F};
    std::array<float, 2> output{};

    if (!expect(buffer.write(discarded.data(), discarded.size()) == discarded.size(), "pre-clear write succeeds"))
        return false;

    buffer.clear();

    return expect(buffer.available() == 0, "clear discards queued samples") &&
           expect(buffer.write(replacement.data(), replacement.size()) == replacement.size(),
                  "write succeeds after clear") &&
           expect(buffer.read(output.data(), output.size()) == output.size(), "read succeeds after clear") &&
           expect(output == replacement, "only post-clear samples are returned");
}

bool test_latency_bound_under_clock_skew()
{
    constexpr size_t channels = 2;
    constexpr size_t sample_rate = 48000;
    constexpr size_t producer_samples_per_tick = 480 * channels;
    constexpr size_t consumer_samples_per_tick = producer_samples_per_tick - 2;
    constexpr size_t max_buffered_samples = sample_rate * channels / 20; // 50 ms
    constexpr size_t simulation_ticks = 48000;                            // 8 minutes

    AudioRingBuffer buffer(sample_rate * channels * 2);
    std::array<float, producer_samples_per_tick> input{};
    std::array<float, consumer_samples_per_tick> output{};
    size_t discarded_samples = 0;

    for (size_t tick = 0; tick < simulation_ticks; ++tick) {
        buffer.write(input.data(), input.size());
        discarded_samples += buffer.trim_to(max_buffered_samples);
        buffer.read(output.data(), output.size());
    }

    const size_t latency_ms = buffer.available() * 1000 / (sample_rate * channels);
    std::cerr << "Observed simulated queue latency: " << latency_ms << " ms\n";
    return expect(discarded_samples > 0, "clock skew triggers stale-sample trimming") &&
           expect(buffer.available() <= max_buffered_samples,
                  "clock skew cannot grow the queue beyond the latency bound");
}

} // namespace

int main()
{
    const bool passed = test_empty_and_invalid_operations() && test_fifo_and_capacity_limit() && test_wraparound() &&
                        test_clear() && test_latency_bound_under_clock_skew();

    if (!passed)
        return EXIT_FAILURE;

    std::cout << "All audio ring buffer tests passed.\n";
    return EXIT_SUCCESS;
}
