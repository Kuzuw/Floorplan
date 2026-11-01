#pragma once

#include <cstdint>

namespace fp::dsp
{

// 2048-point sine table with linear interpolation. Error is below -120 dB,
// which is far cheaper per sample than std::sin and plenty for drum synthesis.
class SineTable
{
public:
    static constexpr int kSize = 2048;

    // Phase is in cycles; any real value is accepted (wrapped internally).
    static inline float sin2pi (float phase) noexcept
    {
        const float* t = table();
        float idx = (phase - (float) (int64_t) phase) * (float) kSize;
        if (idx < 0.0f) idx += (float) kSize;
        if (idx >= (float) kSize) idx = 0.0f;
        const int i0 = (int) idx;
        const float frac = idx - (float) i0;
        return t[i0] + frac * (t[i0 + 1] - t[i0]);
    }

    // Touch the table from a non-real-time thread before audio starts.
    static void warmUp() noexcept { (void) table(); }

private:
    static const float* table() noexcept;
};

} // namespace fp::dsp
