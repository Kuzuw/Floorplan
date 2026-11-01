#pragma once

#include "Instruments/DrumVoice.h"

#include <string>
#include <vector>

namespace fp
{

// Immutable decoded sample. Created and destroyed off the audio thread; the
// audio thread only ever reads it (see SampleSlots in DrumEngine.h).
struct SampleData
{
    std::vector<float> left, right; // right may be empty for mono files
    double sampleRate = 44100.0;
    std::string path;

    bool empty() const noexcept { return left.size() < 2; }
};

// Pitched one-shot playback with linear interpolation and an optional decay.
// Uses its own params pointer layout: [0] = tune (st), [1] = decay (0..100 %).
class SampleVoice final : public DrumVoice
{
public:
    void setSample (const SampleData* s) noexcept
    {
        if (s != sample)
        {
            sample = s;
            active = false; // never read a buffer we no longer own
        }
    }

    bool hasSample() const noexcept { return sample != nullptr && ! sample->empty(); }

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        if (! hasSample())
        {
            active = false;
            return;
        }

        pos = 0.0;
        rate = sample->sampleRate / sampleRate * (double) dsp::semitonesToRatio (p[0] + t.tuneSemis);
        env = 1.0f;
        const float d = p[1] * 0.01f;
        envCoef = d >= 0.999f ? 1.0f : dsp::decayCoef (0.02f * std::pow (500.0f, d) * t.decayMul, sampleRate);
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        if (! hasSample())
        {
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            active = false;
            return;
        }

        const float* srcL = sample->left.data();
        const float* srcR = sample->right.empty() ? srcL : sample->right.data();
        const double last = (double) (sample->left.size() - 1);

        int i = 0;
        for (; i < n && pos < last; ++i)
        {
            const int i0 = (int) pos;
            const float frac = (float) (pos - (double) i0);
            const float g = env * gain;
            L[i] = (srcL[i0] + frac * (srcL[i0 + 1] - srcL[i0])) * g;
            R[i] = (srcR[i0] + frac * (srcR[i0 + 1] - srcR[i0])) * g;
            pos += rate;
            env *= envCoef;
        }

        for (; i < n; ++i)
            L[i] = R[i] = 0.0f;

        if (pos >= last || env < kSilence)
            active = false;
    }

private:
    const SampleData* sample = nullptr;
    double pos = 0.0, rate = 1.0;
    float env = 1.0f, envCoef = 1.0f, gain = 1.0f;
};

} // namespace fp
