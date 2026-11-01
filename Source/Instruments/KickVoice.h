#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Sine body with an exponential pitch sweep, a hold stage for punch and a
// filtered-noise click for the transient.
class KickVoice final : public DrumVoice
{
protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace KickP;
        baseFreq   = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        sweepDepth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
        sweepEnv   = 1.0f;
        sweepCoef  = dsp::timeConstantCoef (p[BendTime] * 0.001f, sampleRate);
        amp        = 1.0f;
        ampCoef    = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        holdLeft   = (int) (p[Punch] * 0.01f * 0.06f * (float) sampleRate);
        clickAmt   = p[Click] * 0.01f;
        clickEnv   = 1.0f;
        clickCoef  = dsp::decayCoef (0.007f, sampleRate);
        clickHp.setup (1800.0f, sampleRate);
        clickHp.reset();
        phase = 0.0f;
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float freq = baseFreq * (1.0f + sweepDepth * sweepEnv);
            sweepEnv *= sweepCoef;
            phase += freq * invSampleRate;
            if (phase >= 1.0f) phase -= 1.0f;

            const float body = dsp::SineTable::sin2pi (phase) * amp;
            if (holdLeft > 0) --holdLeft; else amp *= ampCoef;

            float click = 0.0f;
            if (clickEnv > kSilence)
            {
                click = clickHp.process (noise.next()) * clickEnv * clickAmt * 0.6f;
                clickEnv *= clickCoef;
            }

            const float out = (body + click) * gain;
            L[i] = out;
            R[i] = out;
        }

        if (amp < kSilence && holdLeft <= 0 && clickEnv < kSilence)
            active = false;
    }

private:
    float baseFreq = 50.0f, sweepDepth = 0.0f, sweepEnv = 0.0f, sweepCoef = 0.0f;
    float amp = 0.0f, ampCoef = 0.0f, clickAmt = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;
    float phase = 0.0f, gain = 1.0f;
    int holdLeft = 0;
    dsp::OnePoleHp clickHp;
    dsp::Noise noise;
};

} // namespace fp
