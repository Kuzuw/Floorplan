#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// The clap's amplitude envelope: `Bursts` short hits `Spread` apart, the last
// one ringing out as the tail. Each hit rises linearly over Attack, then decays.
// Shared by the DSP and the editor's envelope view.
//
// Advanced controls: Attack, Burst Length (scales each short hit's decay),
// Tail Level (peak of the tail relative to the hits) and Jitter (random
// variation of the spacing, up to +-half a spread). Their defaults match the
// original fixed envelope.
struct ClapEnvelope
{
    // jitterSeed: the random spacing of one hit. The voice changes it every hit.
    void setup (const float* p, double sampleRate, float decayMul, uint32_t jitterSeed) noexcept
    {
        using namespace ClapP;
        bursts        = std::clamp ((int) std::lround (p[Bursts]), 1, 6);
        burstIndex    = 0;
        spreadSamples = std::max (1, dsp::msToSamples (p[Spread], sampleRate));
        nextBurstAt   = 0;
        counter       = 0;
        tailCoef      = dsp::decayCoef (p[Decay] * 0.001f * decayMul, sampleRate);
        burstCoef     = dsp::decayCoef (std::min (p[Spread] * 1.6f, 30.0f) * 0.001f * (p[BurstLength] * 0.01f), sampleRate);
        attackInc     = 1.0f / std::max (1.0f, p[Attack] * 0.001f * (float) sampleRate);
        tailLevel     = p[TailLevel] * 0.01f;
        jitter        = p[Jitter] * 0.01f * 0.5f;
        jitterNoise.seed (jitterSeed);
        envCoef       = burstCoef;
        target        = 1.0f;
        env           = 0.0f;
        attacking     = false;
    }

    inline float next() noexcept
    {
        if (burstIndex < bursts && counter >= nextBurstAt)
        {
            const bool tail = burstIndex == bursts - 1;
            envCoef = tail ? tailCoef : burstCoef;
            target = tail ? tailLevel : 1.0f;
            attacking = true;
            ++burstIndex;
            nextBurstAt += spreadSamples + (int) (jitterNoise.next() * jitter * (float) spreadSamples);
        }
        ++counter;

        if (attacking)
        {
            // A hit that starts above its peak (a quiet tail) only stops rising.
            if (env >= target)
                attacking = false;
            else
            {
                env += attackInc;
                if (env >= target) { env = target; attacking = false; }
            }
        }
        else
        {
            env *= envCoef;
        }
        return env;
    }

    bool finished() const noexcept { return burstIndex >= bursts && ! attacking && env < 1.0e-4f; }

private:
    float tailCoef = 0.0f, burstCoef = 0.0f, envCoef = 0.0f, env = 0.0f, attackInc = 0.1f;
    float target = 1.0f, tailLevel = 1.0f, jitter = 0.0f;
    int bursts = 4, burstIndex = 0, spreadSamples = 1, nextBurstAt = 0, counter = 0;
    bool attacking = false;
    dsp::Noise jitterNoise;
};

// Band-passed noise shaped by a train of short bursts followed by a tail,
// which is what makes a clap sound like several hands.
class ClapVoice final : public DrumVoice
{
public:
    // Jitter seed of a new voice's first hit; the editor draws that hit.
    static constexpr uint32_t kFirstJitterSeed = 0x5EEDu;

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace ClapP;
        const float centre = p[Tune] * dsp::semitonesToRatio (t.tuneSemis) * t.toneScale (0.3f);
        const float q = 0.8f + p[Tone] * 0.03f;
        bpL.setup (centre, q, sampleRate);
        bpR.setup (centre, q, sampleRate);
        bpL.reset();
        bpR.reset();
        bandGain = bpL.k * 2.2f; // normalise the band-pass peak, then make up level

        envelope.setup (p, sampleRate, t.decayMul, jitterSeed);
        jitterSeed = jitterSeed * 1664525u + 1013904223u; // a new spacing every hit
        width = p[Width] * 0.01f;
        gain  = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool stereo = width > 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const float env = envelope.next();
            const float n1 = noiseA.next();
            const float outL = bpL.bandpass (n1) * bandGain * env;
            const float outR = stereo ? bpR.bandpass (n1 + width * (noiseB.next() - n1)) * bandGain * env : outL;
            L[i] = outL * gain;
            R[i] = outR * gain;
        }

        if (envelope.finished())
            active = false;
    }

private:
    dsp::Svf bpL, bpR;
    dsp::Noise noiseA { 0xC1A9u }, noiseB { 0xBEEFu };
    ClapEnvelope envelope;
    uint32_t jitterSeed = kFirstJitterSeed;
    float bandGain = 1.0f, width = 0.0f, gain = 1.0f;
};

} // namespace fp
