#pragma once

#include <atomic>
#include <cstdint>

namespace fp
{

// Wait-free single-writer / single-reader triple buffer.
//
// The writer fills its private back buffer and publishes it with publish();
// the reader calls acquire() to get the most recently published value. Neither
// side ever blocks or allocates, and the reader never sees a half-written
// value. Used to hand pattern banks from the message thread to the audio thread.
//
// Exactly one thread may call the writer functions (callers serialise them)
// and exactly one thread may call acquire().
template <typename T>
class TripleBuffer
{
public:
    TripleBuffer() = default;

    explicit TripleBuffer (const T& initial)
    {
        for (auto& b : buffers)
            b = initial;
    }

    // Writer side ------------------------------------------------------------
    T& backBuffer() noexcept { return buffers[backIndex]; }

    void publish() noexcept
    {
        // Swap back <-> middle and mark the middle as fresh.
        const uint32_t prev = middle.exchange ((uint32_t) backIndex | kFreshBit, std::memory_order_acq_rel);
        backIndex = (int) (prev & kIndexMask);
    }

    void write (const T& value) noexcept
    {
        backBuffer() = value;
        publish();
    }

    // Reader side ------------------------------------------------------------
    const T& acquire() noexcept
    {
        if ((middle.load (std::memory_order_relaxed) & kFreshBit) != 0)
        {
            const uint32_t prev = middle.exchange ((uint32_t) frontIndex, std::memory_order_acq_rel);
            frontIndex = (int) (prev & kIndexMask);
        }
        return buffers[frontIndex];
    }

private:
    static constexpr uint32_t kFreshBit = 4u;
    static constexpr uint32_t kIndexMask = 3u;

    T buffers[3] {};
    int backIndex = 0;              // writer-owned
    int frontIndex = 1;             // reader-owned
    std::atomic<uint32_t> middle { 2 };
};

} // namespace fp
