#pragma once

// Small, allocation-free DSP building blocks used by the drum voices.
// All of them are plain value types: no virtual calls, no heap, no locks.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fp::dsp
{

inline constexpr float kPi    = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

// Exponential decay coefficient that reaches -60 dB after `seconds`.
inline float decayCoef (float seconds, double sampleRate) noexcept
{
    const double samples = std::max (1.0, (double) seconds * sampleRate);
    return (float) std::exp (-6.907755278982137 / samples); // ln(0.001)
}

// One-pole coefficient for a time constant (reaches 1/e after `seconds`).
inline float timeConstantCoef (float seconds, double sampleRate) noexcept
{
    const double samples = std::max (1.0, (double) seconds * sampleRate);
    return (float) std::exp (-1.0 / samples);
}

inline float dbToGain (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float semitonesToRatio (float st) noexcept { return std::exp2 (st * (1.0f / 12.0f)); }

// Cheap rational tanh approximation, exact at 0, saturates smoothly to +-1.
inline float softClip (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// xorshift32 white noise in [-1, 1). Deterministic and lock-free.
struct Noise
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9E3779B9u; }

    inline float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
};

// Band-limited square using PolyBLEP correction.
struct BlepSquare
{
    float phase = 0.0f;
    float inc   = 0.0f;

    static inline float blep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    inline float next() noexcept
    {
        float out = phase < 0.5f ? 1.0f : -1.0f;
        out += blep (phase, inc);
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        out -= blep (p2, inc);
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

// Topology-preserving-transform state variable filter (Zavalishin / Simper).
// Coefficients are computed only when the cutoff or resonance changes.
struct Svf
{
    float g = 0.0f, k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;

    void setup (float cutoffHz, float q, double sampleRate) noexcept
    {
        const float nyq = (float) sampleRate * 0.49f;
        cutoffHz = std::clamp (cutoffHz, 10.0f, nyq);
        g  = std::tan (kPi * cutoffHz / (float) sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    struct Out { float low, band, high; };

    inline Out process (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }

    inline float lowpass  (float x) noexcept { return process (x).low; }
    inline float bandpass (float x) noexcept { return process (x).band; }
    inline float highpass (float x) noexcept { return process (x).high; }
};

// One-pole high-pass, used for DC blocking and transient shaping.
struct OnePoleHp
{
    float a = 0.995f, x1 = 0.0f, y1 = 0.0f;

    void setup (float cutoffHz, double sampleRate) noexcept
    {
        a = (float) std::exp (-2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate);
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = a * (y1 + x - x1);
        x1 = x;
        y1 = y;
        return y;
    }
};

// Linear ramp used for per-block gain smoothing.
struct LinearRamp
{
    float current = 0.0f;
    bool initialised = false;

    // Returns the per-sample increment that reaches `target` after n samples.
    float begin (float target, int n) noexcept
    {
        if (! initialised) { current = target; initialised = true; }
        return n > 0 ? (target - current) / (float) n : 0.0f;
    }
};

} // namespace fp::dsp
