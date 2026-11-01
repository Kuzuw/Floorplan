#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Band-passed noise shaped by a train of short bursts followed by a tail,
// which is what makes a clap sound like several hands.
class ClapVoice final : public DrumVoice
{
protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace ClapP;
        const float centre = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        const float q = 0.8f + p[Tone] * 0.03f;
        bpL.setup (centre, q, sampleRate);
        bpR.setup (centre, q, sampleRate);
        bpL.reset();
        bpR.reset();
        bandGain = bpL.k * 2.2f; // normalise the band-pass peak, then make up level

        bursts        = std::clamp ((int) std::lround (p[Bursts]), 1, 6);
        burstIndex    = 0;
        spreadSamples = std::max (1, (int) (p[Spread] * 0.001f * (float) sampleRate));
        nextBurstAt   = 0;
        counter       = 0;
        tailCoef      = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        burstCoef     = dsp::decayCoef (std::min (p[Spread] * 1.6f, 30.0f) * 0.001f, sampleRate);
        attackInc     = 1.0f / std::max (1.0f, 0.0004f * (float) sampleRate);
        envCoef       = burstCoef;
        env           = 0.0f;
        attacking     = false;
        width         = p[Width] * 0.01f;
        gain          = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool stereo = width > 0.001f;

        for (int i = 0; i < n; ++i)
        {
            if (burstIndex < bursts && counter >= nextBurstAt)
            {
                envCoef = (burstIndex == bursts - 1) ? tailCoef : burstCoef;
                attacking = true;
                ++burstIndex;
                nextBurstAt += spreadSamples;
            }
            ++counter;

            if (attacking)
            {
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; attacking = false; }
            }
            else
            {
                env *= envCoef;
            }

            const float n1 = noiseA.next();
            const float outL = bpL.bandpass (n1) * bandGain * env;
            const float outR = stereo ? bpR.bandpass (n1 + width * (noiseB.next() - n1)) * bandGain * env : outL;
            L[i] = outL * gain;
            R[i] = outR * gain;
        }

        if (burstIndex >= bursts && ! attacking && env < kSilence)
            active = false;
    }

private:
    dsp::Svf bpL, bpR;
    dsp::Noise noiseA { 0xC1A9u }, noiseB { 0xBEEFu };
    float bandGain = 1.0f, tailCoef = 0.0f, burstCoef = 0.0f, envCoef = 0.0f, env = 0.0f, attackInc = 0.1f;
    float width = 0.0f, gain = 1.0f;
    int bursts = 4, burstIndex = 0, spreadSamples = 1, nextBurstAt = 0, counter = 0;
    bool attacking = false;
};

} // namespace fp
