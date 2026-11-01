#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Two detuned sine modes with a short pitch drop for the shell, plus
// high-passed noise for the wires. Width decorrelates the noise per channel.
class SnareVoice final : public DrumVoice
{
protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace SnareP;
        freq       = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        const float tone = p[Tone] * 0.01f;
        bodyMix    = 1.0f - 0.8f * tone;
        noiseMix   = 0.3f + 0.9f * tone;
        bodyEnv    = 1.0f;
        bodyCoef   = dsp::decayCoef (p[BodyDecay] * 0.001f * t.decayMul, sampleRate);
        noiseEnv   = 1.0f;
        noiseCoef  = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        pitchEnv   = 1.0f;
        pitchCoef  = dsp::timeConstantCoef (0.006f, sampleRate);
        width      = p[Width] * 0.01f;

        const float cutoff = 600.0f * std::pow (10.0f, p[Snappy] * 0.01f); // 600 Hz .. 6 kHz
        hpL.setup (cutoff, 0.7f, sampleRate);
        hpR.setup (cutoff, 0.7f, sampleRate);
        hpL.reset();
        hpR.reset();
        ph1 = ph2 = 0.0f;
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool stereo = width > 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const float sweep = 1.0f + 0.5f * pitchEnv;
            pitchEnv *= pitchCoef;
            ph1 += freq * sweep * invSampleRate;
            ph2 += freq * 1.62f * sweep * invSampleRate;
            if (ph1 >= 1.0f) ph1 -= 1.0f;
            if (ph2 >= 1.0f) ph2 -= 1.0f;

            const float body = (dsp::SineTable::sin2pi (ph1) + 0.45f * dsp::SineTable::sin2pi (ph2)) * bodyEnv * bodyMix;
            bodyEnv *= bodyCoef;

            const float n1 = noiseA.next();
            const float wiresL = hpL.highpass (n1) * noiseEnv * noiseMix;
            const float wiresR = stereo ? hpR.highpass (n1 + width * (noiseB.next() - n1)) * noiseEnv * noiseMix : wiresL;
            noiseEnv *= noiseCoef;

            L[i] = (body + wiresL) * gain * 0.7f;
            R[i] = (body + wiresR) * gain * 0.7f;
        }

        if (bodyEnv < kSilence && noiseEnv < kSilence)
            active = false;
    }

private:
    float freq = 180.0f, bodyMix = 1.0f, noiseMix = 1.0f, width = 0.0f, gain = 1.0f;
    float bodyEnv = 0.0f, bodyCoef = 0.0f, noiseEnv = 0.0f, noiseCoef = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;
    float ph1 = 0.0f, ph2 = 0.0f;
    dsp::Svf hpL, hpR;
    dsp::Noise noiseA, noiseB { 0x1234567u };
};

} // namespace fp
