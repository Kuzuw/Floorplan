#pragma once

#include "Parameters/ParameterSpecs.h"

#include <array>
#include <cstdint>

namespace fp
{

// One step of one track. 8 bytes, trivially copyable, so whole banks can be
// copied between threads with memcpy-like cost.
struct Step
{
    enum Flags : uint8_t { Accent = 1u << 0, Flam = 1u << 1 };

    uint8_t on          = 0;
    uint8_t velocity    = 100; // 1..127
    uint8_t probability = 100; // 0..100 %
    int8_t  micro       = 0;   // -50..+50 % of a step
    uint8_t flags       = 0;
    uint8_t ratchet     = 1;   // 1..4 hits per step
    int8_t  tune        = 0;   // parameter lock, semitones -24..24 (0 = none)
    int8_t  decay       = 0;   // parameter lock, -100..100 => x0.25..x4 (0 = none)

    bool accent() const noexcept { return (flags & Accent) != 0; }
    bool flam() const noexcept   { return (flags & Flam) != 0; }
    void setFlag (Flags f, bool v) noexcept { flags = (uint8_t) (v ? (flags | f) : (flags & ~f)); }
    bool isDefaultExceptOn() const noexcept;
    void sanitise() noexcept;
};

struct Track
{
    std::array<Step, kMaxSteps> steps {};
};

struct Pattern
{
    uint8_t length = 16;
    std::array<Track, kNumVoices> tracks {};

    void clear() noexcept;
    bool isEmpty() const noexcept;
    void clearTrack (int voice) noexcept;
    void rotateTrack (int voice, int amount) noexcept;           // positive = later
    void euclidean (int voice, int pulses, int rotation) noexcept; // over `length` steps
    void doubleLength() noexcept;                                // copies content, max 64
    void randomiseTrack (int voice, float intensity, uint32_t seed) noexcept;
    void sanitise() noexcept;
};

struct PatternBank
{
    std::array<Pattern, kNumPatterns> patterns {};
};

// Euclidean rhythm helper (Bresenham formulation): true where a pulse falls.
bool euclideanHit (int index, int steps, int pulses, int rotation) noexcept;

// A sensible starting pattern: four-on-the-floor house groove.
Pattern makeDefaultHousePattern() noexcept;

} // namespace fp
