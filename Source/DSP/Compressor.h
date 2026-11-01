#pragma once

#include "DSP/DSPCore.h"

namespace fp::dsp
{

// Lightweight stereo-linked bus compressor driven by a single "amount" knob.
// The detector runs per sample; the gain computer (log/exp) runs once every
// kControlInterval samples and is linearly interpolated in between.
class BusCompressor
{
public:
    static constexpr int kControlInterval = 16;

    void prepare (double sr) noexcept
    {
        attack  = timeConstantCoef (0.006f, sr);
        release = timeConstantCoef (0.12f, sr);
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
        gain = targetGain = 1.0f;
        gainInc = 0.0f;
        counter = 0;
    }

    // amount in (0, 1]. Returns the gain reduction of the last sample in dB.
    float process (float* L, float* R, int n, float amount) noexcept
    {
        const float thresholdDb = -amount * 24.0f;
        const float ratio = 2.0f + amount * 4.0f;
        const float slope = 1.0f - 1.0f / ratio;
        const float makeupDb = -thresholdDb * slope * 0.5f;
        float reductionDb = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float level = std::max (std::abs (L[i]), std::abs (R[i]));
            const float coef = level > envelope ? attack : release;
            envelope = level + coef * (envelope - level);

            if (counter == 0)
            {
                const float levelDb = 20.0f * std::log10 (std::max (envelope, 1.0e-6f));
                const float over = levelDb - thresholdDb;
                reductionDb = over > 0.0f ? -over * slope : 0.0f;
                targetGain = std::pow (10.0f, (reductionDb + makeupDb) * 0.05f);
                gainInc = (targetGain - gain) / (float) kControlInterval;
                counter = kControlInterval;
            }

            --counter;
            gain += gainInc;
            L[i] *= gain;
            R[i] *= gain;
        }

        return reductionDb;
    }

private:
    float attack = 0.0f, release = 0.0f, envelope = 0.0f;
    float gain = 1.0f, targetGain = 1.0f, gainInc = 0.0f;
    int counter = 0;
};

} // namespace fp::dsp
