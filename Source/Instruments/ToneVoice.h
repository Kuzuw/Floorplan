#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Tunable pitched percussion: sine carrier with a pitch sweep, optional
// phase modulation ("Metal") for bell / wood colours and a noise transient.
// Used for both the percussion and the low tom slots.
//
// Advanced controls: a linear attack and a hold before the decay, the noise
// transient's high-pass cutoff and length (previously fixed at 1.5 kHz and
// 30 ms), the pitch of the Metal modulator in semitones (Metal Tune) and a
// waveshaper that turns the sine towards a square (Shape). At their defaults
// the voice sounds as it did before.
class ToneVoice final : public DrumVoice
{
public:
    explicit ToneVoice (float fmRatio) : modRatio (fmRatio) {}

    // Envelope models drawn by the editor. They follow the same stages as
    // renderVoice, and tests compare them with the rendered output. t is in
    // seconds; amplitudes are 0..1.
    static float ampEnvelope (const float* p, float t) noexcept
    {
        using namespace ToneP;
        const float attack = p[Attack] * 0.001f;
        if (t < attack)
            return t / attack;
        t -= attack + p[Hold] * 0.001f;
        return t <= 0.0f ? 1.0f : std::exp (-6.907755f * t / std::max (1.0e-3f, p[Decay] * 0.001f));
    }

    static float noiseEnvelope (const float* p, float t) noexcept
    {
        return std::exp (-6.907755f * t / std::max (1.0e-3f, p[ToneP::NoiseLength] * 0.001f));
    }

    // Pitch above the tuned frequency, in semitones.
    static float pitchSemitones (const float* p, float t) noexcept
    {
        using namespace ToneP;
        const float depth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
        const float timeConstant = std::max (1.0e-4f, p[BendTime] * 0.001f);
        return 12.0f * std::log2 (1.0f + depth * std::exp (-t / timeConstant));
    }

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
        holdLeft   = dsp::msToSamples (p[Hold], sampleRate);

        const int attackSamples = dsp::msToSamples (p[Attack], sampleRate);
        attackEnv  = attackSamples > 0 ? 0.0f : 1.0f;
        attackInc  = attackSamples > 0 ? 1.0f / (float) attackSamples : 1.0f;

        shapeDrive = 1.0f + p[Shape] * 0.05f;
        shapeNorm  = 1.0f / dsp::softClip (shapeDrive);
        shaping    = p[Shape] > 0.0f;

        noiseAmt   = p[Noise] * 0.01f * t.toneScale (0.8f);
        noiseEnv   = 1.0f;
        noiseCoef  = dsp::decayCoef (p[NoiseLength] * 0.001f, sampleRate);
        ratio      = modRatio * dsp::semitonesToRatio (p[MetalTune]);
        fmIndex    = p[Metal] * 0.01f * 0.7f * t.toneScale (0.5f); // in cycles
        noiseHp.setup (p[NoiseTone], sampleRate);
        noiseHp.reset();
        phC = phM = 0.0f;
        gain = t.gain;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        // The Advanced stages cost a little on every sample, so a hit that does
        // not use them (or no longer does) takes the plain path.
        if (attackEnv >= 1.0f && holdLeft <= 0 && ! shaping)
            renderHit<false> (L, R, n);
        else
            renderHit<true> (L, R, n);

        if (amp < kSilence && noiseEnv < kSilence && holdLeft <= 0 && attackEnv >= 1.0f)
            active = false;
    }

private:
    template <bool advanced>
    void renderHit (float* L, float* R, int n) noexcept
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
                phM += freq * ratio * invSampleRate;
                if (phM >= 1.0f) phM -= 1.0f;
                mod = dsp::SineTable::sin2pi (phM) * fmIndex * amp;
            }

            float out = dsp::SineTable::sin2pi (phC + mod);
            if constexpr (advanced)
            {
                if (shaping)
                    out = dsp::softClip (out * shapeDrive) * shapeNorm;
                out *= amp * attackEnv;

                // Attack, then hold, then decay.
                if (attackEnv < 1.0f)
                    attackEnv = std::min (1.0f, attackEnv + attackInc);
                else if (holdLeft > 0)
                    --holdLeft;
                else
                    amp *= ampCoef;
            }
            else
            {
                out *= amp;
                amp *= ampCoef;
            }

            if (noiseEnv > kSilence)
            {
                out += noiseHp.process (noise.next()) * noiseEnv * noiseAmt * 0.6f;
                noiseEnv *= noiseCoef;
            }

            out *= gain;
            L[i] = out;
            R[i] = out;
        }
    }

    float modRatio = 1.41f, ratio = 1.41f;
    float baseFreq = 200.0f, sweepDepth = 0.0f, sweepEnv = 0.0f, sweepCoef = 0.0f;
    float amp = 0.0f, ampCoef = 0.0f, attackEnv = 1.0f, attackInc = 1.0f;
    float shapeDrive = 1.0f, shapeNorm = 1.0f;
    bool shaping = false;
    float noiseAmt = 0.0f, noiseEnv = 0.0f, noiseCoef = 0.0f;
    float fmIndex = 0.0f, phC = 0.0f, phM = 0.0f, gain = 1.0f;
    int holdLeft = 0;
    dsp::OnePoleHp noiseHp;
    dsp::Noise noise { 0xD00Du };
};

} // namespace fp
