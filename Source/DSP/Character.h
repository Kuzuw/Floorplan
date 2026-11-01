#pragma once

#include "DSP/DSPCore.h"

namespace fp::dsp
{

// softClip without the input clamp, for constants (inputs well inside +-3).
constexpr float softClipUnclamped (float x) noexcept { return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }

// The master bus Character stage: Punch (a transient shaper), then Color (a
// saturator). It is a generic design, not a model of a particular circuit.
//
// Punch: the stereo power is smoothed twice, over 5 ms (fast) and 20 ms
// (slow). While the fast average is above the slow one the level is rising:
// that is an attack. Sustained and decaying sound leaves the fast average at
// or below the slow one and is not changed. A positive amount boosts attacks
// by up to 6 dB; a negative amount cuts them by up to 6 dB, which also lowers
// very short sounds such as closed hats overall.
//
// Color: parallel saturation. The signal goes through a soft clipper with a
// small DC bias, which makes it asymmetric, so it adds even as well as odd
// harmonics. A DC blocker removes the bias. Quiet signals pass at about unity
// gain (-0.3 dB); louder peaks are rounded off progressively (a -10 dBFS peak by about 1 dB,
// a full-scale one by about 3 to 6 dB), so turning Color up never makes
// anything louder. Color crossfades from the clean signal (0) to the
// saturated one (1).
//
// Every setting moves linearly across a block, so automation does not click.
// The whole stage fades in and out over one block when it is switched on or
// off. Once fully off it is skipped, so the bus is bit-identical to having no
// stage at all.
class CharacterStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        fastCoef    = timeConstantCoef (0.005f, sampleRate);
        slowCoef    = timeConstantCoef (0.020f, sampleRate);
        gainSmooth  = timeConstantCoef (0.0007f, sampleRate);
        dcL.setup (8.0f, sampleRate);
        dcR.setup (8.0f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        mix = color = punch = 0.0f;
        clearState();
    }

    bool isIdle() const noexcept { return mix == 0.0f; }

    // on: the section switch. color 0..1, punch -1..1.
    void process (float* L, float* R, int n, bool on, float newColor, float newPunch) noexcept
    {
        const float targetMix = on ? 1.0f : 0.0f;
        if (mix == 0.0f && targetMix == 0.0f)
            return;
        if (mix == 0.0f)
            clearState(); // the stage was bypassed: no stale envelopes

        const float inv = 1.0f / (float) n;
        const float dMix = (targetMix - mix) * inv;
        const float dColor = (newColor - color) * inv;
        const float dPunch = (newPunch - punch) * inv;
        const bool punching = newPunch != 0.0f || punch != 0.0f;
        const bool colouring = newColor != 0.0f || color != 0.0f;

        float m = mix, c = color, k = punch;
        for (int i = 0; i < n; ++i)
        {
            m += dMix;
            c += dColor;
            k += dPunch;
            const float dryL = L[i], dryR = R[i];
            float l = dryL, r = dryR;

            if (punching)
            {
                // The tiny offset keeps the averages out of denormals and makes
                // silence read as steady (ratio 1).
                const float power = 0.5f * (l * l + r * r) + 1.0e-12f;
                fastEnv = power + fastCoef * (fastEnv - power);
                slowEnv = power + slowCoef * (slowEnv - power);
                const float transient = std::clamp (std::sqrt (fastEnv / slowEnv) - 1.0f, 0.0f, 1.0f);
                const float target = k >= 0.0f ? 1.0f + k * transient : 1.0f / (1.0f - k * transient);
                gain = target + gainSmooth * (gain - target);
                l *= gain;
                r *= gain;
            }

            if (colouring)
            {
                l += c * dcL.process (saturate (l) - l);
                r += c * dcR.process (saturate (r) - r);
            }

            L[i] = dryL + m * (l - dryL);
            R[i] = dryR + m * (r - dryR);
        }

        mix = targetMix;
        color = newColor;
        punch = newPunch;
        if (! punching)
            gain = 1.0f;
    }

    // The saturator alone (exposed for tests): |saturate (x)| <= |x|.
    static float saturate (float x) noexcept
    {
        return (softClip (kDrive * x + kBias) - kBiasOffset) * kNorm;
    }

private:
    static constexpr float kDrive = 1.5f, kBias = 0.2f;
    static constexpr float kBiasOffset = softClipUnclamped (kBias);
    // The clipper's steepest slope (1, at its own zero) maps to unity, so no
    // input comes out louder. At the bias point the slope is ~0.97 (-0.3 dB).
    static constexpr float kNorm = 1.0f / kDrive;

    void clearState() noexcept
    {
        fastEnv = slowEnv = 1.0e-12f;
        gain = 1.0f;
        dcL.reset();
        dcR.reset();
    }

    float fastCoef = 0.0f, slowCoef = 0.0f, gainSmooth = 0.0f;
    float fastEnv = 1.0e-12f, slowEnv = 1.0e-12f, gain = 1.0f; // mean power, 5 ms and 20 ms
    float mix = 0.0f, color = 0.0f, punch = 0.0f;
    OnePoleHp dcL, dcR;
};

} // namespace fp::dsp
