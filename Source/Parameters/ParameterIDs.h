#pragma once

#include "Parameters/ParameterSpecs.h"

#include <juce_core/juce_core.h>

// String IDs of every automatable parameter. These are stored in DAW projects
// and presets: never rename one, only add new ones.
namespace fp::ids
{

inline const juce::String masterVolume { "master_volume" };
inline const juce::String masterClip   { "master_clip" };
inline const juce::String masterComp   { "master_comp" };
inline const juce::String accent       { "accent" };
inline const juce::String swing        { "swing" };
inline const juce::String groove       { "groove" };
inline const juce::String resolution   { "resolution" };
inline const juce::String pattern      { "pattern" };
inline const juce::String chainOn      { "chain_on" };
inline const juce::String tempo        { "tempo" };
inline const juce::String randAmount   { "rand_amount" };
inline const juce::String seed         { "prob_seed" };
inline const juce::String midiOut      { "midi_out" };
inline const juce::String sequencerOn  { "seq_on" };

inline juce::String chainSlot (int slot) { return "chain_" + juce::String (slot + 1); }

inline juce::String voice (int v, const char* suffix)
{
    return juce::String (kVoiceSpecs[(size_t) v].id) + "_" + suffix;
}

// p in [0, numVoiceParams (v)): the main six, then the engine's extras.
inline juce::String synth (int v, int p)
{
    return voice (v, voiceParamSpec (v, p).id);
}

inline juce::String level (int v)       { return voice (v, "level"); }
inline juce::String pan (int v)         { return voice (v, "pan"); }
inline juce::String mute (int v)        { return voice (v, "mute"); }
inline juce::String solo (int v)        { return voice (v, "solo"); }
inline juce::String filter (int v)      { return voice (v, "filter"); }
inline juce::String drive (int v)       { return voice (v, "drive"); }
inline juce::String output (int v)      { return voice (v, "out"); }
inline juce::String sampleTune (int v)  { return voice (v, "stune"); }
inline juce::String sampleDecay (int v) { return voice (v, "sdecay"); }

} // namespace fp::ids
