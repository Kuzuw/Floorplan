#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Tunable pitched percussion: sine carrier with a pitch sweep, optional
// phase modulation ("Metal") for bell / wood colours and a noise transient.
// Used for both the percussion and the low tom slots.
class ToneVoice final : public DrumVoice
{
public:
    explicit ToneVoice (float fmRatio) : modRatio (fmRatio) {}

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace ToneP;
        baseFreq   = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        sweepDepth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
        sweepEnv   = 1.0f;
        sweepCoef  = dsp::timeConstantCoef (p[BendTime] * 0.001f, sampleRate);
        amp        = 1.0f;
        ampCoef    = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        noiseAmt   = p[Noise] * 0.01f;
        noiseEnv   = 1.0f;
        noiseCoef  = dsp::decayCoef (0.03f, sampleRate);
        fmIndex    = p[Metal] * 0.01f * 0.7f; // in cycles
        noiseHp.setup (1500.0f, sampleRate);
        noiseHp.reset();
        phC = phM = 0.0f;
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool fm = fmIndex > 0.0001f;

        for (int i = 0; i < n; ++i)
        {
            const float freq = baseFreq * (1.0f + sweepDepth * sweepEnv);
            sweepEnv *= sweepCoef;
            phC += freq * invSampleRate;
            if (phC >= 1.0f) phC -= 1.0f;

            float mod = 0.0f;
            if (fm)
            {
                phM += freq * modRatio * invSampleRate;
                if (phM >= 1.0f) phM -= 1.0f;
                mod = dsp::SineTable::sin2pi (phM) * fmIndex * amp;
            }

            float out = dsp::SineTable::sin2pi (phC + mod) * amp;
            amp *= ampCoef;

            if (noiseEnv > kSilence)
            {
                out += noiseHp.process (noise.next()) * noiseEnv * noiseAmt * 0.6f;
                noiseEnv *= noiseCoef;
            }

            out *= gain;
            L[i] = out;
            R[i] = out;
        }

        if (amp < kSilence && noiseEnv < kSilence)
            active = false;
    }

private:
    float modRatio = 1.41f;
    float baseFreq = 200.0f, sweepDepth = 0.0f, sweepEnv = 0.0f, sweepCoef = 0.0f;
    float amp = 0.0f, ampCoef = 0.0f, noiseAmt = 0.0f, noiseEnv = 0.0f, noiseCoef = 0.0f;
    float fmIndex = 0.0f, phC = 0.0f, phM = 0.0f, gain = 1.0f;
    dsp::OnePoleHp noiseHp;
    dsp::Noise noise { 0xD00Du };
};

} // namespace fp
