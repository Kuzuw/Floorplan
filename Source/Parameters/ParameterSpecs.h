#pragma once

// Central, JUCE-free description of every drum voice and its synthesis
// parameters. The plugin layer builds its AudioProcessorValueTreeState layout
// from this table and the engine interprets the values in the same order, so
// adding or retuning a voice only needs changes here and in the voice class.
//
// IDs are part of the saved-state format: never rename an existing ID.

#include <array>
#include <cstdint>

namespace fp
{

inline constexpr int kNumVoices      = 8;
inline constexpr int kNumSynthParams = 6;
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

// Index meaning for each engine. Keep in sync with the tables below.
namespace KickP  { enum { Tune, Bend, BendTime, Decay, Click, Punch }; }
namespace SnareP { enum { Tune, Tone, Decay, Snappy, BodyDecay, Width }; }
namespace ClapP  { enum { Tune, Decay, Spread, Bursts, Tone, Width }; }
namespace MetalP { enum { Tune, Decay, Bright, Metal, Tone, Width }; }
namespace ToneP  { enum { Tune, Bend, BendTime, Decay, Noise, Metal }; }

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

// Groove and sequencer option tables (index == stored parameter value).
inline constexpr std::array<const char*, 5> kResolutionNames { "1/8", "1/16", "1/32", "1/8T", "1/16T" };
inline constexpr std::array<double, 5>      kResolutionPpq   { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
inline constexpr std::array<const char*, 6> kGrooveNames     { "Straight", "Shuffle", "Laid Back", "Push", "Humanize", "Broken" };
inline constexpr std::array<int, 6>         kLengthChoices   { 8, 12, 16, 24, 32, 64 };

} // namespace fp
