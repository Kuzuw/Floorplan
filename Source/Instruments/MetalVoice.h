#pragma once

#include "Instruments/DrumVoice.h"
#include "Parameters/ParameterSpecs.h"

#include <array>

namespace fp
{

// Six band-limited square oscillators at inharmonic ratios blended with
// noise, then high-passed and coloured by a resonant band. Used for closed
// hat, open hat and cymbal with different partial sets and ranges.
//
// Advanced controls: a linear attack and a hold before the decay, a second,
// four times slower decay mixed in underneath (Tail), Detune (moves the
// partials away from their fixed ratios), a low-pass on the whole voice
// (High Cut, off at its maximum) and Stick, a short click of high-passed
// noise at the start of the hit. At their defaults these stages are bypassed,
// so the voice sounds as it did before.
class MetalVoice final : public DrumVoice
{
public:
    explicit MetalVoice (const std::array<float, 6>& partialsHz) : partials (partialsHz) {}

    static constexpr float kTailTimeScale = 4.0f;
    static constexpr float kMaxTailMix = 0.5f;

    static float tailMix (const float* p) noexcept { return p[MetalP::Tail] * 0.01f * kMaxTailMix; }

    // Envelope model drawn by the editor. It follows the same stages as
    // renderVoice (attack, hold, decay to -60 dB plus the tail), and tests
    // compare it with the rendered output. t is in seconds.
    static float ampEnvelope (const float* p, float t) noexcept
    {
        using namespace MetalP;
        const float attack = p[Attack] * 0.001f;
        if (t < attack)
            return t / attack;
        t -= attack + p[Hold] * 0.001f;
        if (t <= 0.0f)
            return 1.0f;
        const float decay = std::max (1.0e-3f, p[Decay] * 0.001f);
        const float fast = std::exp (-6.907755f * t / decay);
        const float slow = std::exp (-6.907755f * t / (decay * kTailTimeScale));
        return fast + tailMix (p) * (slow - fast);
    }

protected:
    void onTrigger (const float* p, const TriggerInfo& t) override
    {
        using namespace MetalP;
        // Fixed offsets per partial, scaled by Detune (up to about +-10%).
        static constexpr std::array<float, 6> kDetuneSpread { -0.09f, 0.06f, -0.04f, 0.08f, -0.07f, 0.11f };
        const float tune = p[Tune] * dsp::semitonesToRatio (t.tuneSemis);
        const float detune = p[Detune] * 0.01f;
        for (size_t i = 0; i < oscs.size(); ++i)
        {
            oscs[i].inc = std::min (0.45f, partials[i] * (1.0f + detune * kDetuneSpread[i]) * tune * invSampleRate);
            oscs[i].phase = 0.13f * (float) i; // fixed spread keeps hits consistent
        }

        const float decaySeconds = p[Decay] * 0.001f * t.decayMul;
        env      = 1.0f;
        envCoef  = dsp::decayCoef (decaySeconds, sampleRate);
        tailAmt  = tailMix (p);
        tailEnv  = 1.0f;
        tailCoef = dsp::decayCoef (decaySeconds * kTailTimeScale, sampleRate);
        holdLeft = dsp::msToSamples (p[Hold], sampleRate);

        const int attackSamples = dsp::msToSamples (p[Attack], sampleRate);
        attackEnv = attackSamples > 0 ? 0.0f : 1.0f;
        attackInc = attackSamples > 0 ? 1.0f / (float) attackSamples : 1.0f;

        metalMix = p[Metal] * 0.01f;
        toneAmt  = p[Tone] * 0.01f;
        width    = p[Width] * 0.01f;

        const float bright = p[Bright] * std::sqrt (tune) * t.toneScale (0.6f);
        hpL.setup (bright, 0.7f, sampleRate);
        hpR.setup (bright, 0.7f, sampleRate);
        bpL.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sampleRate);
        bpR.setup (bright * 1.2f, 1.0f + toneAmt * 5.0f, sampleRate);
        hpL.reset(); hpR.reset(); bpL.reset(); bpR.reset();
        bandGain = bpL.k * toneAmt * 1.5f;

        highCut = p[HighCut] < kHighCutOff;
        lpL.setup (p[HighCut], 0.7f, sampleRate);
        lpR.setup (p[HighCut], 0.7f, sampleRate);
        lpL.reset(); lpR.reset();

        stickAmt  = p[Stick] * 0.01f * 0.8f * t.toneScale (0.8f);
        stickEnv  = stickAmt > 0.0f ? 1.0f : 0.0f;
        stickCoef = dsp::decayCoef (0.004f, sampleRate);
        stickHp.setup (4000.0f, sampleRate);
        stickHp.reset();
        gain = t.gain * 0.8f;
    }

    void renderVoice (float* L, float* R, int n) override
    {
        // The Advanced stages cost a little on every sample, so a hit that does
        // not use them (or no longer does) takes the plain path.
        const bool tailing = tailAmt > 0.0f;
        if (attackEnv >= 1.0f && holdLeft <= 0 && ! tailing && stickEnv < kSilence && ! highCut)
            renderHit<false> (L, R, n);
        else
            renderHit<true> (L, R, n);

        const bool tailDone = ! tailing || tailEnv * tailAmt < kSilence;
        if (env < kSilence && tailDone && holdLeft <= 0 && attackEnv >= 1.0f && stickEnv < kSilence)
            active = false;
    }

private:
    template <bool advanced>
    void renderHit (float* L, float* R, int n) noexcept
    {
        const bool stereo = width > 0.001f;
        const bool tailing = tailAmt > 0.0f;
        const float noiseMix = (1.0f - metalMix) * 0.8f;

        for (int i = 0; i < n; ++i)
        {
            float metallic = 0.0f;
            for (auto& o : oscs)
                metallic += o.next();
            metallic *= metalMix * (1.0f / 6.0f);

            float e = env;
            if constexpr (advanced)
            {
                if (tailing)
                    e += tailAmt * (tailEnv - env);
                e *= attackEnv;
            }

            const float srcL = metallic + noiseMix * noiseA.next();
            const auto fL = hpL.process (srcL);
            float outL = (fL.high + bpL.bandpass (srcL) * bandGain) * e;

            float outR = outL;
            if (stereo)
            {
                const float srcR = metallic + noiseMix * noiseB.next();
                const float mixR = srcR + (1.0f - width) * (srcL - srcR);
                outR = (hpR.highpass (mixR) + bpR.bandpass (mixR) * bandGain) * e;
            }

            if constexpr (advanced)
            {
                // Attack, then hold, then decay.
                if (attackEnv < 1.0f)
                    attackEnv = std::min (1.0f, attackEnv + attackInc);
                else if (holdLeft > 0)
                    --holdLeft;
                else
                {
                    env *= envCoef;
                    tailEnv *= tailCoef;
                }

                if (stickEnv > kSilence)
                {
                    const float stick = stickHp.process (stickNoise.next()) * stickEnv * stickAmt;
                    outL += stick;
                    outR += stick;
                    stickEnv *= stickCoef;
                }

                if (highCut)
                {
                    outL = lpL.lowpass (outL);
                    outR = stereo ? lpR.lowpass (outR) : outL;
                }
            }
            else
                env *= envCoef;

            L[i] = outL * gain;
            R[i] = outR * gain;
        }
    }

    static constexpr float kHighCutOff = 19999.0f;

    std::array<float, 6> partials;
    std::array<dsp::BlepSquare, 6> oscs {};
    dsp::Svf hpL, hpR, bpL, bpR, lpL, lpR;
    dsp::OnePoleHp stickHp;
    dsp::Noise noiseA { 0x51A7u }, noiseB { 0x7E11u }, stickNoise { 0x571Cu };
    float env = 0.0f, envCoef = 0.0f, tailAmt = 0.0f, tailEnv = 0.0f, tailCoef = 0.0f;
    float attackEnv = 1.0f, attackInc = 1.0f;
    float metalMix = 0.5f, toneAmt = 0.0f, width = 0.0f, bandGain = 0.0f, gain = 1.0f;
    float stickAmt = 0.0f, stickEnv = 0.0f, stickCoef = 0.0f;
    int holdLeft = 0;
    bool highCut = false;
};

} // namespace fp
