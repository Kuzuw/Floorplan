#pragma once

// Central, JUCE-free description of every drum voice and its synthesis
// parameters. The plugin layer builds its AudioProcessorValueTreeState layout
// from this table and the engine interprets the values in the same order, so
// adding or retuning a voice only needs changes here and in the voice class.
//
// IDs are part of the saved-state format: never rename an existing ID.

#include <array>
#include <cstdint>
#include <iterator>

namespace fp
{

inline constexpr int kNumVoices      = 8;
inline constexpr int kNumSynthParams = 6;  // main controls of every voice (Standard Mode)
inline constexpr int kMaxExtraParams = 8;  // Advanced Mode controls an engine may add
inline constexpr int kMaxVoiceParams = kNumSynthParams + kMaxExtraParams;
inline constexpr int kMaxSteps       = 64;
inline constexpr int kNumPatterns    = 16;
inline constexpr int kChainSlots     = 8;

enum class EngineKind : uint8_t { Kick, Snare, Clap, Metal, Tone };

struct SynthParamSpec
{
    const char* id;        // suffix, combined with the voice id: "kick_tune"
    const char* name;      // short label shown on the knob
    const char* unit;      // "Hz", "ms", "st", "%" or ""
    float min, max, def;
    float centre;          // skew so that this value sits mid-knob (0 = linear)
};

struct VoiceSpec
{
    const char* id;
    const char* name;
    const char* shortName;
    EngineKind engine;
    int defaultMidiNote;
    float defaultLevelDb;
    float defaultPan;
    std::array<SynthParamSpec, kNumSynthParams> params;
};

// Index meaning for each engine. Keep in sync with the tables below. Indices
// from kNumSynthParams on are the engine's extra (Advanced Mode) parameters.
namespace KickP  { enum { Tune, Bend, BendTime, Decay, Click, Punch, Attack, Shape, Sub, ClickTone, ClickLength }; }
namespace SnareP { enum { Tune, Tone, Decay, Snappy, BodyDecay, Width, Drop, DropTime, Ring, WireTone, WireDelay }; }
namespace ClapP  { enum { Tune, Decay, Spread, Bursts, Tone, Width, Attack, BurstLength, TailLevel, Jitter }; }
namespace MetalP { enum { Tune, Decay, Bright, Metal, Tone, Width, Attack, Hold, Tail, Detune, HighCut, Stick }; }
namespace ToneP  { enum { Tune, Bend, BendTime, Decay, Noise, Metal, Attack, Hold, NoiseTone, NoiseLength, MetalTune, Shape }; }

inline constexpr std::array<VoiceSpec, kNumVoices> kVoiceSpecs {{
    { "kick", "Kick", "BD", EngineKind::Kick, 36, -3.0f, 0.0f, {{
        { "tune",     "Tune",   "Hz", 30.0f,  120.0f,  48.0f,  55.0f },
        { "bend",     "Bend",   "st", 0.0f,   48.0f,   26.0f,  0.0f  },
        { "bendtime", "Bend T", "ms", 2.0f,   200.0f,  32.0f,  35.0f },
        { "decay",    "Decay",  "ms", 60.0f,  2500.0f, 420.0f, 450.0f },
        { "click",    "Click",  "%",  0.0f,   100.0f,  35.0f,  0.0f  },
        { "punch",    "Punch",  "%",  0.0f,   100.0f,  30.0f,  0.0f  } }} },

    { "snare", "Snare", "SD", EngineKind::Snare, 38, -6.0f, 0.0f, {{
        { "tune",      "Tune",   "Hz", 100.0f, 400.0f,  185.0f, 190.0f },
        { "tone",      "Tone",   "%",  0.0f,   100.0f,  45.0f,  0.0f   },
        { "decay",     "Decay",  "ms", 40.0f,  1000.0f, 220.0f, 220.0f },
        { "snappy",    "Snappy", "%",  0.0f,   100.0f,  60.0f,  0.0f   },
        { "bodydecay", "Body",   "ms", 20.0f,  400.0f,  110.0f, 100.0f },
        { "width",     "Width",  "%",  0.0f,   100.0f,  25.0f,  0.0f   } }} },

    { "chh", "Closed Hat", "CH", EngineKind::Metal, 42, -9.0f, 0.12f, {{
        { "tune",   "Tune",   "x",  0.5f,    2.0f,     1.0f,    1.0f    },
        { "decay",  "Decay",  "ms", 10.0f,   400.0f,   55.0f,   60.0f   },
        { "bright", "Bright", "Hz", 2000.0f, 14000.0f, 7500.0f, 6000.0f },
        { "metal",  "Metal",  "%",  0.0f,    100.0f,   60.0f,   0.0f    },
        { "tone",   "Tone",   "%",  0.0f,    100.0f,   35.0f,   0.0f    },
        { "width",  "Width",  "%",  0.0f,    100.0f,   20.0f,   0.0f    } }} },

    { "ohh", "Open Hat", "OH", EngineKind::Metal, 46, -10.0f, -0.12f, {{
        { "tune",   "Tune",   "x",  0.5f,    2.0f,     1.0f,    1.0f    },
        { "decay",  "Decay",  "ms", 60.0f,   2500.0f,  380.0f,  400.0f  },
        { "bright", "Bright", "Hz", 2000.0f, 14000.0f, 6500.0f, 6000.0f },
        { "metal",  "Metal",  "%",  0.0f,    100.0f,   65.0f,   0.0f    },
        { "tone",   "Tone",   "%",  0.0f,    100.0f,   40.0f,   0.0f    },
        { "width",  "Width",  "%",  0.0f,    100.0f,   35.0f,   0.0f    } }} },

    { "clap", "Clap", "CP", EngineKind::Clap, 39, -6.0f, 0.0f, {{
        { "tune",   "Tune",   "Hz", 600.0f, 3000.0f, 1150.0f, 1200.0f },
        { "decay",  "Decay",  "ms", 50.0f,  1200.0f, 260.0f,  260.0f  },
        { "spread", "Spread", "ms", 3.0f,   25.0f,   9.0f,    9.0f    },
        { "bursts", "Bursts", "",   1.0f,   6.0f,    4.0f,    0.0f    },
        { "tone",   "Tone",   "%",  0.0f,   100.0f,  50.0f,   0.0f    },
        { "width",  "Width",  "%",  0.0f,   100.0f,  45.0f,   0.0f    } }} },

    { "perc", "Percussion", "PC", EngineKind::Tone, 37, -9.0f, 0.25f, {{
        { "tune",     "Tune",   "Hz", 150.0f, 1800.0f, 520.0f, 500.0f },
        { "bend",     "Bend",   "st", 0.0f,   24.0f,   4.0f,   0.0f   },
        { "bendtime", "Bend T", "ms", 5.0f,   300.0f,  40.0f,  50.0f  },
        { "decay",    "Decay",  "ms", 20.0f,  1500.0f, 170.0f, 200.0f },
        { "noise",    "Noise",  "%",  0.0f,   100.0f,  15.0f,  0.0f   },
        { "metal",    "Metal",  "%",  0.0f,   100.0f,  25.0f,  0.0f   } }} },

    { "tom", "Low Tom", "LT", EngineKind::Tone, 41, -7.0f, -0.2f, {{
        { "tune",     "Tune",   "Hz", 50.0f,  300.0f,  105.0f, 110.0f },
        { "bend",     "Bend",   "st", 0.0f,   24.0f,   9.0f,   0.0f   },
        { "bendtime", "Bend T", "ms", 5.0f,   300.0f,  60.0f,  60.0f  },
        { "decay",    "Decay",  "ms", 60.0f,  2000.0f, 380.0f, 400.0f },
        { "noise",    "Noise",  "%",  0.0f,   100.0f,  10.0f,  0.0f   },
        { "metal",    "Metal",  "%",  0.0f,   100.0f,  0.0f,   0.0f   } }} },

    { "cym", "Cymbal", "CY", EngineKind::Metal, 49, -14.0f, 0.3f, {{
        { "tune",   "Tune",   "x",  0.5f,    2.0f,     1.0f,    1.0f    },
        { "decay",  "Decay",  "ms", 300.0f,  5000.0f,  1600.0f, 1500.0f },
        { "bright", "Bright", "Hz", 2000.0f, 14000.0f, 5000.0f, 6000.0f },
        { "metal",  "Metal",  "%",  0.0f,    100.0f,   55.0f,   0.0f    },
        { "tone",   "Tone",   "%",  0.0f,    100.0f,   50.0f,   0.0f    },
        { "width",  "Width",  "%",  0.0f,    100.0f,   60.0f,   0.0f    } }} },
}};

// Voice indices, matching kVoiceSpecs order.
enum VoiceIndex { Kick = 0, Snare, ClosedHat, OpenHat, Clap, Perc, Tom, Cymbal };

// Extra synthesis parameters per engine, shown only in Advanced Mode. They
// follow the main six in the array a voice receives. Every default leaves the
// sound exactly as it was before the parameter existed, so older projects and
// kits (which do not store them) sound unchanged.
inline constexpr SynthParamSpec kKickExtras[] {
    { "attack",    "Attack",    "ms", 0.0f,   30.0f,    0.0f,    6.0f    },
    { "shape",     "Shape",     "%",  0.0f,   100.0f,   0.0f,    0.0f    },
    { "sub",       "Sub",       "%",  0.0f,   100.0f,   0.0f,    0.0f    },
    { "clicktone", "Click Tone", "Hz", 500.0f, 10000.0f, 1800.0f, 2500.0f },
    { "clicklen",  "Click Len", "ms", 1.0f,   50.0f,    7.0f,    10.0f   },
};

// Wire Tone is a low-pass on the wires; at its maximum it is switched off.
inline constexpr SynthParamSpec kSnareExtras[] {
    { "drop",      "Drop",       "%",  0.0f,    100.0f,   50.0f,    0.0f    },
    { "droptime",  "Drop Time",  "ms", 1.0f,    50.0f,    6.0f,     10.0f   },
    { "ring",      "Ring",       "%",  0.0f,    100.0f,   45.0f,    0.0f    },
    { "wiretone",  "Wire Tone",  "Hz", 2000.0f, 20000.0f, 20000.0f, 8000.0f },
    { "wiredelay", "Wire Delay", "ms", 0.0f,    20.0f,    0.0f,     0.0f    },
};

inline constexpr SynthParamSpec kClapExtras[] {
    { "attack",    "Attack",    "ms", 0.1f,  5.0f,   0.4f,   1.0f   },
    { "burstlen",  "Burst Len", "%",  25.0f, 400.0f, 100.0f, 100.0f },
    { "taillevel", "Tail Level", "%",  0.0f,  100.0f, 100.0f, 0.0f   },
    { "jitter",    "Jitter",    "%",  0.0f,  100.0f, 0.0f,   0.0f   },
};

// High Cut is a low-pass on the whole voice; at its maximum it is switched off.
inline constexpr SynthParamSpec kMetalExtras[] {
    { "attack",  "Attack",   "ms", 0.0f,    500.0f,   0.0f,     40.0f   },
    { "hold",    "Hold",     "ms", 0.0f,    500.0f,   0.0f,     60.0f   },
    { "tail",    "Tail",     "%",  0.0f,    100.0f,   0.0f,     0.0f    },
    { "detune",  "Detune",   "%",  0.0f,    100.0f,   0.0f,     0.0f    },
    { "highcut", "High Cut", "Hz", 2000.0f, 20000.0f, 20000.0f, 8000.0f },
    { "stick",   "Stick",    "%",  0.0f,    100.0f,   0.0f,     0.0f    },
};

inline constexpr SynthParamSpec kToneExtras[] {
    { "attack",    "Attack",     "ms", 0.0f,   30.0f,    0.0f,    6.0f    },
    { "hold",      "Hold",       "ms", 0.0f,   200.0f,   0.0f,    40.0f   },
    { "noisetone", "Noise Tone", "Hz", 200.0f, 12000.0f, 1500.0f, 2000.0f },
    { "noiselen",  "Noise Len",  "ms", 2.0f,   300.0f,   30.0f,   40.0f   },
    { "metaltune", "Metal Tune", "st", -24.0f, 24.0f,    0.0f,    0.0f    },
    { "shape",     "Shape",      "%",  0.0f,   100.0f,   0.0f,    0.0f    },
};

struct ParamList
{
    const SynthParamSpec* data;
    int count;
};

constexpr ParamList extraParams (EngineKind engine) noexcept
{
    switch (engine)
    {
        case EngineKind::Kick:  return { kKickExtras, (int) std::size (kKickExtras) };
        case EngineKind::Snare: return { kSnareExtras, (int) std::size (kSnareExtras) };
        case EngineKind::Clap:  return { kClapExtras, (int) std::size (kClapExtras) };
        case EngineKind::Metal: return { kMetalExtras, (int) std::size (kMetalExtras) };
        case EngineKind::Tone:  return { kToneExtras, (int) std::size (kToneExtras) };
        default:                return { nullptr, 0 };
    }
}

static_assert (std::size (kKickExtras) <= (size_t) kMaxExtraParams);
static_assert (std::size (kSnareExtras) <= (size_t) kMaxExtraParams);
static_assert (std::size (kClapExtras) <= (size_t) kMaxExtraParams);
static_assert (std::size (kMetalExtras) <= (size_t) kMaxExtraParams);
static_assert (std::size (kToneExtras) <= (size_t) kMaxExtraParams);

// The order engines' extras were added in. The plugin appends each group
// after every parameter that existed before it, so a parameter's index never
// moves (some hosts store automation by index). Append new groups only.
constexpr int extrasGeneration (EngineKind engine) noexcept
{
    return engine == EngineKind::Metal || engine == EngineKind::Tone ? 1 : 0;
}

inline constexpr int kNumExtrasGenerations = 2;

// All synthesis parameters of a voice: the main six, then the extras.
constexpr int numVoiceParams (int voice) noexcept
{
    return kNumSynthParams + extraParams (kVoiceSpecs[(size_t) voice].engine).count;
}

constexpr const SynthParamSpec& voiceParamSpec (int voice, int index) noexcept
{
    const auto& spec = kVoiceSpecs[(size_t) voice];
    return index < kNumSynthParams ? spec.params[(size_t) index]
                                   : extraParams (spec.engine).data[index - kNumSynthParams];
}

// Advanced Mode inspector page a synthesis parameter is shown on. Engines
// with a Pitch/Env page list its controls here, in display order (amplitude
// stages first, then pitch); everything else is on the Source page.
enum class ParamPage : uint8_t { Source, PitchEnvelope };

inline constexpr int kKickEnvelopePage[]  { KickP::Attack, KickP::Punch, KickP::Decay, KickP::Bend, KickP::BendTime };
inline constexpr int kSnareEnvelopePage[] { SnareP::BodyDecay, SnareP::Decay, SnareP::WireDelay, SnareP::Drop, SnareP::DropTime };
inline constexpr int kClapEnvelopePage[]  { ClapP::Bursts, ClapP::Spread, ClapP::Jitter, ClapP::Attack, ClapP::BurstLength, ClapP::Decay };
inline constexpr int kMetalEnvelopePage[] { MetalP::Attack, MetalP::Hold, MetalP::Decay, MetalP::Tail };
inline constexpr int kToneEnvelopePage[]  { ToneP::Attack, ToneP::Hold, ToneP::Decay, ToneP::NoiseLength, ToneP::Bend, ToneP::BendTime };

struct IndexList
{
    const int* data;
    int count;
};

constexpr IndexList envelopePage (EngineKind engine) noexcept
{
    switch (engine)
    {
        case EngineKind::Kick:  return { kKickEnvelopePage, (int) std::size (kKickEnvelopePage) };
        case EngineKind::Snare: return { kSnareEnvelopePage, (int) std::size (kSnareEnvelopePage) };
        case EngineKind::Clap:  return { kClapEnvelopePage, (int) std::size (kClapEnvelopePage) };
        case EngineKind::Metal: return { kMetalEnvelopePage, (int) std::size (kMetalEnvelopePage) };
        case EngineKind::Tone:  return { kToneEnvelopePage, (int) std::size (kToneEnvelopePage) };
        default:                return { nullptr, 0 };
    }
}

constexpr bool hasEnvelopePage (EngineKind engine) noexcept { return envelopePage (engine).count > 0; }

constexpr ParamPage paramPage (EngineKind engine, int index) noexcept
{
    const auto page = envelopePage (engine);
    for (int i = 0; i < page.count; ++i)
        if (page.data[i] == index)
            return ParamPage::PitchEnvelope;
    return ParamPage::Source;
}

// Groove and sequencer option tables (index == stored parameter value).
inline constexpr std::array<const char*, 5> kResolutionNames { "1/8", "1/16", "1/32", "1/8T", "1/16T" };
inline constexpr std::array<double, 5>      kResolutionPpq   { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
inline constexpr std::array<const char*, 6> kGrooveNames     { "Straight", "Shuffle", "Laid Back", "Push", "Humanize", "Broken" };
inline constexpr std::array<int, 6>         kLengthChoices   { 8, 12, 16, 24, 32, 64 };

} // namespace fp
