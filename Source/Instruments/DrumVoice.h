#pragma once

#include "DSP/DSPCore.h"
#include "DSP/SineTable.h"

#include <algorithm>
#include <memory>

namespace fp
{

// Everything a voice needs to know about a single hit.
struct TriggerInfo
{
    float gain      = 1.0f; // velocity * accent, already mapped to linear gain
    float tuneSemis = 0.0f; // per-step parameter lock (0 = none)
    float decayMul  = 1.0f; // per-step parameter lock (1 = none)
};

// Base class for a monophonic drum voice.
//
// Synthesis parameters are latched at trigger time (like most hardware drum
// machines); continuous controls such as level, pan, filter and drive live in
// the engine's channel strip. A voice costs nothing while idle: render() fills
// silence and returns as soon as the voice has decayed below -80 dB.
//
// Threading: owned and used exclusively by the audio thread after prepare().
class DrumVoice
{
public:
    virtual ~DrumVoice() = default;

    virtual void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        invSampleRate = (float) (1.0 / newSampleRate);
        declickCoef = dsp::timeConstantCoef (0.0012f, newSampleRate);
        chokeCoef = dsp::timeConstantCoef (0.004f, newSampleRate);
        reset();
    }

    virtual void reset()
    {
        active = false;
        choking = false;
        chokeGain = 1.0f;
        lastL = lastR = declickL = declickR = 0.0f;
    }

    void trigger (const float* params, const TriggerInfo& info)
    {
        // Retriggering a sounding voice would jump from its last output value
        // to the new sound's first sample. Bridge the step with a short
        // decaying offset instead of crossfading two voices.
        if (active || std::abs (declickL) + std::abs (declickR) > 1.0e-5f)
        {
            declickL = lastL;
            declickR = lastR;
        }

        choking = false;
        chokeGain = 1.0f;
        active = true;
        onTrigger (params, info);
    }

    void choke() noexcept
    {
        if (active)
            choking = true;
    }

    // Overwrites [0, n) of L and R.
    void render (float* L, float* R, int n)
    {
        if (n <= 0)
            return;

        if (active)
            renderVoice (L, R, n);
        else
            std::fill (L, L + n, 0.0f), std::fill (R, R + n, 0.0f);

        if (choking)
        {
            for (int i = 0; i < n; ++i)
            {
                L[i] *= chokeGain;
                R[i] *= chokeGain;
                chokeGain *= chokeCoef;
            }

            if (chokeGain < 1.0e-4f)
            {
                active = false;
                choking = false;
            }
        }

        if (declickL != 0.0f || declickR != 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                L[i] += declickL;
                R[i] += declickR;
                declickL *= declickCoef;
                declickR *= declickCoef;
            }

            if (std::abs (declickL) + std::abs (declickR) < 1.0e-6f)
                declickL = declickR = 0.0f;
        }

        lastL = L[n - 1];
        lastR = R[n - 1];
    }

    bool isActive() const noexcept { return active || declickL != 0.0f || declickR != 0.0f; }

protected:
    virtual void onTrigger (const float* params, const TriggerInfo& info) = 0;
    virtual void renderVoice (float* L, float* R, int n) = 0; // must write all n samples

    double sampleRate = 48000.0;
    float invSampleRate = 1.0f / 48000.0f;
    bool active = false;

    static constexpr float kSilence = 1.0e-4f; // -80 dB

private:
    bool choking = false;
    float chokeGain = 1.0f, chokeCoef = 0.99f;
    float lastL = 0.0f, lastR = 0.0f, declickL = 0.0f, declickR = 0.0f, declickCoef = 0.99f;
};

// Creates the synthesis engine for a slot of kVoiceSpecs (see Voices.cpp).
std::unique_ptr<DrumVoice> createSynthVoice (int voiceIndex);

} // namespace fp
