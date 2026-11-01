#pragma once

#include "DSP/Character.h"
#include "DSP/Compressor.h"
#include "Instruments/DrumVoice.h"
#include "Instruments/SampleVoice.h"
#include "Parameters/ParameterSpecs.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace fp
{

// Continuous (per-block) settings for one channel strip.
struct VoiceParams
{
    std::array<float, kMaxVoiceParams> synth {}; // [0, numVoiceParams) used
    float levelDb = 0.0f, pan = 0.0f;
    float filter = 0.0f;  // -1 = low-pass closed, 0 = bypass, +1 = high-pass open
    float drive = 0.0f;   // 0..1
    bool mute = false, solo = false;
    int output = 0;       // 0 = main mix, 1..8 = aux bus n
    int chokedBy = -1;    // voice whose hits cut this one off; -1 = none
    float sampleTune = 0.0f, sampleDecay = 100.0f;
};

struct MasterParams
{
    float volumeDb = 0.0f;
    float clip = 0.2f;     // 0 = off
    float comp = 0.0f;     // 0 = off
    float accent = 0.5f;   // accent boost amount

    // Character section. Off leaves every hit and the bus bit-identical.
    bool character = false;
    float color = 0.3f;    // 0..1 saturation
    float punch = 0.0f;    // -1..1 transient shaping
    float vary = 0.3f;     // 0..1 per-hit variation
    float velTone = 0.5f;  // 0..1 how much softer hits are also duller
    uint32_t seed = 1;     // variation seed (shared with probability)
};

struct TriggerEvent
{
    int offset = 0;        // sample position inside the block
    uint8_t voice = 0;
    float velocity = 1.0f; // 0..1
    bool accent = false;
    int8_t tune = 0;       // semitones
    int8_t decay = 0;      // -100..100 -> x0.25..x4
    uint32_t hitId = 0;    // identifies the hit for repeatable variation; 0 = take the next free id
};

// Lock-free single-producer/single-consumer hand-off of decoded samples.
//
// Message thread: publish() and collectGarbage().
// Audio thread:   acquire() once per block.
// Ownership moves message -> pending -> active (audio) -> retired -> deleted
// on the message thread, so the audio thread never frees memory.
class SampleSlots
{
public:
    ~SampleSlots();

    void publish (int voice, std::unique_ptr<SampleData> data); // data may hold an empty sample to clear
    void collectGarbage();
    const SampleData* acquire (int voice) noexcept;              // audio thread

private:
    std::array<std::atomic<SampleData*>, kNumVoices> pending {};
    std::array<std::atomic<SampleData*>, kNumVoices> retired {};
    std::array<SampleData*, kNumVoices> active {};
};

// The complete sound engine: 8 voices, channel strips, routing and master bus.
// Real-time safe after prepare(): no allocation, locks or system calls.
class DrumEngine
{
public:
    static constexpr int kNumAuxBuses = kNumVoices;

    DrumEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    VoiceParams& voiceParams (int v) noexcept { return voiceSettings[(size_t) v]; }
    MasterParams& masterParams() noexcept { return master; }
    void setSample (int v, const SampleData* s) noexcept { samplers[(size_t) v].setSample (s); }

    // Fades out every sounding voice within a few milliseconds (MIDI All Sound Off).
    void chokeAll() noexcept;
    bool voiceUsesSample (int v) const noexcept { return samplers[(size_t) v].hasSample(); }

    // events: sorted by offset, offsets in [0, numSamples).
    // aux[i] may be null pairs; voices routed to an unavailable bus fall back to main.
    void process (const TriggerEvent* events, int numEvents,
                  float* mainL, float* mainR,
                  float* const* auxL, float* const* auxR,
                  int numSamples);

    float voicePeak (int v) const noexcept { return voicePeaks[(size_t) v]; }
    float masterPeakL() const noexcept { return peakL; }
    float masterPeakR() const noexcept { return peakR; }

    // Per-hit gain from velocity and accent (exposed for tests).
    static float velocityToGain (float velocity, bool accent, float accentAmount) noexcept;

    // Limits of the per-hit variation at Vary = 100%.
    static constexpr float kVaryTuneSemis = 0.2f, kVaryDecay = 0.12f, kVaryLevelDb = 1.0f, kVaryTone = 0.15f;

    // Applies the Character section's per-hit changes to `info`: velocity to
    // tone, then the variation, which depends only on (seed, hitId, voice), so
    // a hit sounds the same on every playback and every render. Does nothing
    // when the section is off. Exposed for tests.
    static void applyCharacter (TriggerInfo& info, float velocity, int voice, uint32_t hitId, const MasterParams& m) noexcept;

private:
    struct Strip
    {
        dsp::Svf filterL, filterR;
        float cachedFilter = 0.0f;
        double cachedRate = 0.0;
        dsp::LinearRamp gainL, gainR;
    };

    void renderVoice (int v, const TriggerEvent* events, int numEvents, int numSamples);

    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::array<std::unique_ptr<DrumVoice>, kNumVoices> synths;
    std::array<SampleVoice, kNumVoices> samplers;
    std::array<VoiceParams, kNumVoices> voiceSettings;
    std::array<Strip, kNumVoices> strips;
    std::array<float, kNumVoices> voicePeaks {};
    MasterParams master;
    dsp::CharacterStage character;
    dsp::BusCompressor compressor;
    uint32_t nextFreeHitId = 0;
    dsp::LinearRamp masterGain;
    std::vector<float> scratchL, scratchR;
    float peakL = 0.0f, peakR = 0.0f;
};

} // namespace fp
