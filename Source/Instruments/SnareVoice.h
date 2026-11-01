#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Two detuned sine modes with a short pitch drop for the shell, plus
// high-passed noise for the wires. Width decorrelates the noise per channel.
//
// Advanced controls: the shell's pitch drop and its time, the level of the
// upper mode (Ring), a low-pass on the wires (off at its maximum) and a delay
// before the wires sound. At their defaults the snare sounds as it did before.
class SnareVoice final : public DrumVoice
{
public:
    // Envelope models drawn by the editor (t in seconds, all values 0..1).
    static float bodyEnvelope (const float* p, float t) noexcept
    {
        return std::exp (-6.907755f * t / std::max (1.0e-3f, p[SnareP::BodyDecay] * 0.001f));
    }

    static float wireEnvelope (const float* p, float t) noexcept
    {
        using namespace SnareP;
        t -= p[WireDelay] * 0.001f;
        return t < 0.0f ? 0.0f : std::exp (-6.907755f * t / std::max (1.0e-3f, p[Decay] * 0.001f));
    }

    // Shell pitch above its tuned frequency, as a fraction (1 = one octave).
    static float pitchDrop (const float* p, float t) noexcept
    {
        using namespace SnareP;
        return p[Drop] * 0.01f * std::exp (-t / std::max (1.0e-4f, p[DropTime] * 0.001f));
    }

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace SnareP;
        freq       = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        const float tone = p[Tone] * 0.01f;
        bodyMix    = 1.0f - 0.8f * tone;
        noiseMix   = 0.3f + 0.9f * tone;
        ring       = p[Ring] * 0.01f;
        bodyEnv    = 1.0f;
        bodyCoef   = dsp::decayCoef (p[BodyDecay] * 0.001f * t.decayMul, sampleRate);
        noiseEnv   = 1.0f;
        noiseCoef  = dsp::decayCoef (p[Decay] * 0.001f * t.decayMul, sampleRate);
        wireWait   = (int) std::ceil (p[WireDelay] * 0.001 * sampleRate); // first sample at or after the delay
        dropDepth  = p[Drop] * 0.01f;
        pitchEnv   = 1.0f;
        pitchCoef  = dsp::timeConstantCoef (p[DropTime] * 0.001f, sampleRate);
        width      = p[Width] * 0.01f;

        const float cutoff = 600.0f * std::pow (10.0f, p[Snappy] * 0.01f); // 600 Hz .. 6 kHz
        hpL.setup (cutoff, 0.7f, sampleRate);
        hpR.setup (cutoff, 0.7f, sampleRate);
        hpL.reset();
        hpR.reset();

        wireLowPass = p[WireTone] < kWireToneOff;
        lpL.setup (p[WireTone], 0.7f, sampleRate);
        lpR.setup (p[WireTone], 0.7f, sampleRate);
        lpL.reset();
        lpR.reset();
        ph1 = ph2 = 0.0f;
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        const bool stereo = width > 0.001f;

        for (int i = 0; i < n; ++i)
        {
            const float sweep = 1.0f + dropDepth * pitchEnv;
            pitchEnv *= pitchCoef;
            ph1 += freq * sweep * invSampleRate;
            ph2 += freq * 1.62f * sweep * invSampleRate;
            if (ph1 >= 1.0f) ph1 -= 1.0f;
            if (ph2 >= 1.0f) ph2 -= 1.0f;

            const float body = (dsp::SineTable::sin2pi (ph1) + ring * dsp::SineTable::sin2pi (ph2)) * bodyEnv * bodyMix;
            bodyEnv *= bodyCoef;

            // The wires' filters run during the delay so they start settled.
            const float n1 = noiseA.next();
            float rawL = hpL.highpass (n1);
            float rawR = stereo ? hpR.highpass (n1 + width * (noiseB.next() - n1)) : rawL;
            if (wireLowPass)
            {
                rawL = lpL.lowpass (rawL);
                rawR = stereo ? lpR.lowpass (rawR) : rawL;
            }

            float wireEnv = 0.0f;
            if (wireWait > 0)
                --wireWait;
            else
            {
                wireEnv = noiseEnv;
                noiseEnv *= noiseCoef;
            }

            const float wiresL = rawL * wireEnv * noiseMix;
            const float wiresR = stereo ? rawR * wireEnv * noiseMix : wiresL;

            L[i] = (body + wiresL) * gain * 0.7f;
            R[i] = (body + wiresR) * gain * 0.7f;
        }

        if (bodyEnv < kSilence && noiseEnv < kSilence && wireWait <= 0)
            active = false;
    }

private:
    static constexpr float kWireToneOff = 19999.0f;

    float freq = 180.0f, bodyMix = 1.0f, noiseMix = 1.0f, ring = 0.45f, width = 0.0f, gain = 1.0f;
    float bodyEnv = 0.0f, bodyCoef = 0.0f, noiseEnv = 0.0f, noiseCoef = 0.0f;
    float dropDepth = 0.5f, pitchEnv = 0.0f, pitchCoef = 0.0f;
    float ph1 = 0.0f, ph2 = 0.0f;
    int wireWait = 0;
    bool wireLowPass = false;
    dsp::Svf hpL, hpR, lpL, lpR;
    dsp::Noise noiseA, noiseB { 0x1234567u };
};

} // namespace fp
