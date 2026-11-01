#pragma once

#include "Audio/DrumEngine.h"
#include "Sequencer/Pattern.h"

#include <array>
#include <cstdint>

namespace fp
{

// Musical position for the current block, taken from the host play head (or
// from the internal sample-counting clock when there is no host transport).
struct TransportState
{
    bool playing = false;
    double ppq = 0.0;   // quarter notes since song start at the first sample of the block
    double bpm = 120.0;
};

struct SequencerSettings
{
    int pattern = 0;            // requested pattern; switches at the next cycle boundary
    int resolution = 1;         // index into kResolutionPpq
    float swing = 50.0f;        // 50..75 %
    int groove = 0;             // index into kGrooveNames
    bool chainEnabled = false;
    std::array<int8_t, kChainSlots> chain { 0, -1, -1, -1, -1, -1, -1, -1 }; // -1 = empty slot
    uint32_t seed = 1;          // probability / humanise seed
};

// Converts patterns into sample-accurate trigger events.
//
// Timing model: every step k has a nominal position k * stepLength in PPQ,
// shifted by swing, groove and per-step microtiming. Each block covers the
// half-open PPQ window [start, end); an event fires in the block whose window
// contains its time. Consecutive windows are made exactly contiguous, so no
// event can fire twice or be skipped. Host position jumps (loop, seek,
// restart) re-align pattern cycles to the song position.
//
// Threading: audio thread only.
class Sequencer
{
public:
    static constexpr int kMaxEventsPerBlock = 512;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept { running = false; }

    // Writes up to maxEvents events sorted by offset; returns the count.
    int process (const PatternBank& bank, const TransportState& transport,
                 const SequencerSettings& settings, int numSamples,
                 TriggerEvent* out, int maxEvents) noexcept;

    int currentStep() const noexcept { return displayStep; }       // -1 when stopped
    int currentPattern() const noexcept { return displayPattern; }

    // Timing offset in steps for absolute step k (swing + groove), exposed for tests.
    static double grooveOffset (int64_t k, const SequencerSettings& s) noexcept;
    static bool probabilityPasses (int64_t k, int voice, int probability, uint32_t seed) noexcept;

private:
    struct Location { int pattern; int step; bool valid; };

    void resync (int64_t absStep, const PatternBank& bank, const SequencerSettings& settings) noexcept;
    Location locate (int64_t k, const PatternBank& bank, const SequencerSettings& settings) noexcept;
    int nextPattern (const SequencerSettings& settings) noexcept;
    int chainCount (const SequencerSettings& settings) const noexcept;

    double sampleRate = 48000.0;
    bool running = false;
    double prevEnd = 0.0;
    int lastResolution = -1;

    int64_t cycleStart = 0, prevCycleStart = 0;
    int curPattern = 0, prevPattern = 0;
    int chainPos = 0;

    int displayStep = -1, displayPattern = 0;
};

} // namespace fp
