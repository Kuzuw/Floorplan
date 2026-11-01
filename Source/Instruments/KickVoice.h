#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

namespace fp
{

// Sine body with an exponential pitch sweep, a hold stage for punch and a
// filtered-noise click for the transient.
//
// Advanced controls: a linear attack before the hold, a waveshaper that turns
// the sine towards a square (Shape), a clean sine layer at the tuned pitch
// with a 1.5x longer tail (Sub), and the click's high-pass cutoff and length.
// At their defaults these stages are bypassed or match the original fixed
// values (7 ms click at 1.8 kHz), so the kick sounds as it did before.
class KickVoice final : public DrumVoice
{
public:
    // Envelope model drawn by the editor. It follows the same stages as
    // renderVoice (attack, hold, decay to -60 dB), and tests compare it with
    // the rendered output. `p` holds all kick parameters; t is in seconds.
    static float ampEnvelope (const float* p, float t) noexcept
    {
        using namespace KickP;
        const float attack = p[Attack] * 0.001f;
        const float hold = p[Punch] * 0.01f * 0.06f;
        const float decay = std::max (1.0e-3f, p[Decay] * 0.001f);
        if (t < attack)
            return t / attack;
        t -= attack + hold;
        return t <= 0.0f ? 1.0f : std::exp (-6.907755f * t / decay);
    }

    // Pitch above the tuned frequency, in semitones.
    static float pitchSemitones (const float* p, float t) noexcept
    {
        using namespace KickP;
        const float depth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
        const float timeConstant = std::max (1.0e-4f, p[BendTime] * 0.001f);
        return 12.0f * std::log2 (1.0f + depth * std::exp (-t / timeConstant));
    }

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace KickP;
        baseFreq   = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        sweepDepth = dsp::semitonesToRatio (p[Bend]) - 1.0f;
        sweepEnv   = 1.0f;
        sweepCoef  = dsp::timeConstantCoef (p[BendTime] * 0.001f, sampleRate);

        const float decaySeconds = p[Decay] * 0.001f * t.decayMul;
        amp        = 1.0f;
        ampCoef    = dsp::decayCoef (decaySeconds, sampleRate);
        holdLeft   = dsp::msToSamples (p[Punch] * 0.6f, sampleRate); // 0..60 ms

        const int attackSamples = dsp::msToSamples (p[Attack], sampleRate);
        attackEnv  = attackSamples > 0 ? 0.0f : 1.0f;
        attackInc  = attackSamples > 0 ? 1.0f / (float) attackSamples : 1.0f;

        shapeDrive = 1.0f + p[Shape] * 0.05f;
        shapeNorm  = 1.0f / dsp::softClip (shapeDrive);
        shaping    = p[Shape] > 0.0f;

        subLevel   = p[Sub] * 0.01f * 0.8f;
        subAmp     = 1.0f;
        subCoef    = dsp::decayCoef (decaySeconds * 1.5f, sampleRate);
        subPhase   = 0.0f;

        clickAmt   = p[Click] * 0.01f;
        clickEnv   = 1.0f;
        clickCoef  = dsp::decayCoef (p[ClickLength] * 0.001f, sampleRate);
        clickHp.setup (p[ClickTone], sampleRate);
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

            float wave = dsp::SineTable::sin2pi (phase);
            if (shaping)
                wave = dsp::softClip (wave * shapeDrive) * shapeNorm;
            float body = wave * amp * attackEnv;

            if (subLevel > 0.0f)
            {
                body += dsp::SineTable::sin2pi (subPhase) * subAmp * attackEnv * subLevel;
                subPhase += baseFreq * invSampleRate;
                if (subPhase >= 1.0f) subPhase -= 1.0f;
            }

            // Attack, then hold, then decay.
            if (attackEnv < 1.0f)
                attackEnv = std::min (1.0f, attackEnv + attackInc);
            else if (holdLeft > 0)
                --holdLeft;
            else
            {
                amp *= ampCoef;
                subAmp *= subCoef;
            }

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

        const bool subDone = subLevel <= 0.0f || subAmp * subLevel < kSilence;
        if (amp < kSilence && subDone && holdLeft <= 0 && attackEnv >= 1.0f && clickEnv < kSilence)
            active = false;
    }

private:
    float baseFreq = 50.0f, sweepDepth = 0.0f, sweepEnv = 0.0f, sweepCoef = 0.0f;
    float amp = 0.0f, ampCoef = 0.0f, attackEnv = 1.0f, attackInc = 1.0f;
    float shapeDrive = 1.0f, shapeNorm = 1.0f;
    bool shaping = false;
    float subLevel = 0.0f, subAmp = 0.0f, subCoef = 0.0f, subPhase = 0.0f;
    float clickAmt = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;
    float phase = 0.0f, gain = 1.0f;
    int holdLeft = 0;
    dsp::OnePoleHp clickHp;
    dsp::Noise noise;
};

} // namespace fp
