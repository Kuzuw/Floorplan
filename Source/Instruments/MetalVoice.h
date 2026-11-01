#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

#include <array>

namespace fp
{

// Six band-limited square oscillators at inharmonic ratios blended with
// noise, then high-passed and coloured by a resonant band. Used for closed
// hat, open hat and cymbal with different partial sets and ranges.
class MetalVoice final : public DrumVoice
{
public:
    explicit MetalVoice (const std::array<float, 6>& partialsHz) : partials (partialsHz) {}

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace MetalP;
        const float tune = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        for (size_t i = 0; i < oscs.size(); ++i)
        {
            oscs[i].inc = std::min (0.45f, partials[i] * tune * invSampleRate);
            oscs[i].phase = 0.13f * (float) i; // fixed spread keeps hits consistent
        }

        env      = 1.0f;
        envCoef  = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        metalMix = p[Metal] * 0.01f;
        toneAmt  = p[Tone] * 0.01f;
        width    = p[Width] * 0.01f;

        const float bright = p[Bright] * std::sqrt (tune);
        hpL.setup (bright, 0.7f, sampleRate);
        hpR.setup (bright, 0.7f, sampleRate);
        bpL.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sampleRate);
        bpR.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sampleRate);
        hpL.reset(); hpR.reset(); bpL.reset(); bpR.reset();
        bandGain = bpL.k * toneAmt * 1.5f;
        gain = t.gain * 0.8f;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool stereo = width > 0.001f;
        const float noiseMix = (1.0f - metalMix) * 0.8f;

        for (int i = 0; i < n; ++i)
        {
            float metallic = 0.0f;
            for (auto& o : oscs)
                metallic += o.next();
            metallic *= metalMix * (1.0f / 6.0f);

            const float srcL = metallic + noiseMix * noiseA.next();
            const auto fL = hpL.process (srcL);
            const float outL = (fL.high + bpL.bandpass (srcL) * bandGain) * env;

            float outR = outL;
            if (stereo)
            {
                const float srcR = metallic + noiseMix * noiseB.next();
                const float mixR = srcR + (1.0f - width) * (srcL - srcR);
                outR = (hpR.highpass (mixR) + bpR.bandpass (mixR) * bandGain) * env;
            }

            env *= envCoef;
            L[i] = outL * gain;
            R[i] = outR * gain;
        }

        if (env < kSilence)
            active = false;
    }

private:
    std::array<float, 6> partials;
    std::array<dsp::BlepSquare, 6> oscs {};
    dsp::Svf hpL, hpR, bpL, bpR;
    dsp::Noise noiseA { 0x51A7u }, noiseB { 0x7E11u };
    float env = 0.0f, envCoef = 0.0f, metalMix = 0.5f, toneAmt = 0.0f, width = 0.0f, bandGain = 0.0f, gain = 1.0f;
};

} // namespace fp
