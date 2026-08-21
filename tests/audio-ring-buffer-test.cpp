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

} // namespace

int main()
{
    const bool passed = test_empty_and_invalid_operations() && test_fifo_and_capacity_limit() && test_wraparound() &&
                        test_clear();

    if (!passed)
        return EXIT_FAILURE;

    std::cout << "All audio ring buffer tests passed.\n";
    return EXIT_SUCCESS;
}
