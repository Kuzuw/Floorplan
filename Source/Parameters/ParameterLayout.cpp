#include "Parameters/ParameterLayout.h"
#include "Parameters/ParameterIDs.h"

namespace fp
{

namespace
{
using Apvts = juce::AudioProcessorValueTreeState;
constexpr int kParamVersion = 1;

juce::String formatValue (float v, const juce::String& unit)
{
    if (unit == "Hz")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
    if (unit == "ms")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 20.0f ? 1 : 0) + " ms";
    if (unit == "st")  return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
    if (unit == "%")   return juce::String (juce::roundToInt (v)) + "%";
    if (unit == "x")   return juce::String (v, 2) + "x";
    if (unit == "dB")  return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    return juce::String (juce::roundToInt (v));
}

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id, const juce::String& name,
                                                      float min, float max, float def, float centre,
                                                      const juce::String& unit, float interval = 0.0f)
{
    juce::NormalisableRange<float> range (min, max, interval);
    if (centre > min && centre < max)
        range.setSkewForCentre (centre);

    auto attributes = juce::AudioParameterFloatAttributes()
                          .withLabel (unit)
                          .withStringFromValueFunction ([unit] (float v, int) { return formatValue (v, unit); });

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, kParamVersion }, name, range, def, attributes);
}

std::unique_ptr<juce::AudioParameterFloat> makePan (const juce::String& id, const juce::String& name, float def)
{
    auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        const int pct = juce::roundToInt (std::abs (v) * 100.0f);
        return pct == 0 ? juce::String ("C") : (v < 0.0f ? "L" : "R") + juce::String (pct);
    });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, kParamVersion }, name,
                                                        juce::NormalisableRange<float> (-1.0f, 1.0f), def, attributes);
}

std::unique_ptr<juce::AudioParameterBool> makeBool (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, kParamVersion }, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& id, const juce::String& name,
                                                        const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, kParamVersion }, name, choices, def);
}

template <size_t N>
juce::StringArray toStringArray (const std::array<const char*, N>& names)
{
    juce::StringArray a;
    for (auto* n : names) a.add (n);
    return a;
}

std::atomic<float>* raw (Apvts& s, const juce::String& id)
{
    auto* p = s.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

inline float load (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Apvts::ParameterLayout layout;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& spec = kVoiceSpecs[(size_t) v];
        const juce::String vName (spec.name);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (spec.id, vName, " | ");

        for (int p = 0; p < kNumSynthParams; ++p)
        {
            const auto& ps = spec.params[(size_t) p];
            const bool stepped = juce::String (ps.id) == "bursts";
            group->addChild (makeFloat (ids::synth (v, p), vName + " " + ps.name, ps.min, ps.max, ps.def, ps.centre,
                                        ps.unit, stepped ? 1.0f : 0.0f));
        }

        group->addChild (makeFloat (ids::level (v), vName + " Level", -60.0f, 6.0f, spec.defaultLevelDb, -12.0f, "dB"));
        group->addChild (makePan (ids::pan (v), vName + " Pan", spec.defaultPan));
        group->addChild (makeBool (ids::mute (v), vName + " Mute", false));
        group->addChild (makeBool (ids::solo (v), vName + " Solo", false));
        group->addChild (makeFloat (ids::filter (v), vName + " Filter", -100.0f, 100.0f, 0.0f, 0.0f, "%"));
        group->addChild (makeFloat (ids::drive (v), vName + " Drive", 0.0f, 100.0f, 0.0f, 0.0f, "%"));

        // Output n + 2 is named after voice n, the voice it is meant for.
        juce::StringArray outs { "Main" };
        for (int b = 0; b < kNumVoices; ++b)
            outs.add ("Out " + juce::String (b + 2) + " (" + kVoiceSpecs[(size_t) b].name + ")");
        group->addChild (makeChoice (ids::output (v), vName + " Output", outs, 0));
        group->addChild (makeFloat (ids::sampleTune (v), vName + " Sample Tune", -24.0f, 24.0f, 0.0f, 0.0f, "st"));
        group->addChild (makeFloat (ids::sampleDecay (v), vName + " Sample Decay", 0.0f, 100.0f, 100.0f, 0.0f, "%"));

        layout.add (std::move (group));
    }

    auto master = std::make_unique<juce::AudioProcessorParameterGroup> ("master", "Master", " | ");
    master->addChild (makeFloat (ids::masterVolume, "Master Volume", -60.0f, 6.0f, 0.0f, -12.0f, "dB"));
    master->addChild (makeFloat (ids::masterClip, "Master Clip", 0.0f, 100.0f, 20.0f, 0.0f, "%"));
    master->addChild (makeFloat (ids::masterComp, "Master Comp", 0.0f, 100.0f, 0.0f, 0.0f, "%"));
    master->addChild (makeFloat (ids::accent, "Accent Amount", 0.0f, 100.0f, 50.0f, 0.0f, "%"));
    layout.add (std::move (master));

    auto seq = std::make_unique<juce::AudioProcessorParameterGroup> ("sequencer", "Sequencer", " | ");
    seq->addChild (makeFloat (ids::swing, "Swing", 50.0f, 75.0f, 54.0f, 0.0f, "%"));
    seq->addChild (makeChoice (ids::groove, "Groove", toStringArray (kGrooveNames), 0));
    seq->addChild (makeChoice (ids::resolution, "Step Resolution", toStringArray (kResolutionNames), 1));
    seq->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::pattern, kParamVersion }, "Pattern", 1, kNumPatterns, 1));
    seq->addChild (makeBool (ids::chainOn, "Chain Enabled", false));

    juce::StringArray chainChoices { "--" };
    for (int p = 1; p <= kNumPatterns; ++p)
        chainChoices.add (juce::String (p));
    for (int c = 0; c < kChainSlots; ++c)
        seq->addChild (makeChoice (ids::chainSlot (c), "Chain " + juce::String (c + 1), chainChoices, c == 0 ? 1 : 0));

    seq->addChild (makeFloat (ids::tempo, "Internal Tempo", 60.0f, 200.0f, 124.0f, 0.0f, "BPM", 0.1f));
    seq->addChild (makeFloat (ids::randAmount, "Random Intensity", 0.0f, 100.0f, 50.0f, 0.0f, "%"));
    seq->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::seed, kParamVersion }, "Variation Seed", 1, 999, 1));
    seq->addChild (makeBool (ids::midiOut, "MIDI Output", false));
    layout.add (std::move (seq));

    // Advanced Mode synthesis parameters come last, so adding them never moves
    // the index of an existing parameter (some hosts store automation by index).
    // Each generation of extras goes after the ones before it.
    auto advanced = std::make_unique<juce::AudioProcessorParameterGroup> ("advanced", "Advanced", " | ");
    for (int gen = 0; gen < kNumExtrasGenerations; ++gen)
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (extrasGeneration (kVoiceSpecs[(size_t) v].engine) != gen)
                continue;
            const juce::String vName (kVoiceSpecs[(size_t) v].name);
            for (int p = kNumSynthParams; p < numVoiceParams (v); ++p)
            {
                const auto& ps = voiceParamSpec (v, p);
                advanced->addChild (makeFloat (ids::synth (v, p), vName + " " + ps.name, ps.min, ps.max, ps.def, ps.centre, ps.unit));
            }
        }
    layout.add (std::move (advanced));

    // Added after the Advanced extras, for the same reason.
    auto options = std::make_unique<juce::AudioProcessorParameterGroup> ("options", "Options", " | ");
    options->addChild (makeBool (ids::sequencerOn, "Sequencer On", true));
    layout.add (std::move (options));

    return layout;
}

bool isKitParameter (const juce::String& id)
{
    if (id.startsWith ("master_") || id == ids::accent)
        return true;

    for (const auto& spec : kVoiceSpecs)
        if (id.startsWith (juce::String (spec.id) + "_"))
            return ! (id.endsWith ("_mute") || id.endsWith ("_solo") || id.endsWith ("_out"));

    return false;
}

ParameterCache::ParameterCache (Apvts& s)
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vp = voices[(size_t) v];
        for (int p = 0; p < numVoiceParams (v); ++p)
            vp.synth[(size_t) p] = raw (s, ids::synth (v, p));
        vp.level       = raw (s, ids::level (v));
        vp.pan         = raw (s, ids::pan (v));
        vp.mute        = raw (s, ids::mute (v));
        vp.solo        = raw (s, ids::solo (v));
        vp.filter      = raw (s, ids::filter (v));
        vp.drive       = raw (s, ids::drive (v));
        vp.output      = raw (s, ids::output (v));
        vp.sampleTune  = raw (s, ids::sampleTune (v));
        vp.sampleDecay = raw (s, ids::sampleDecay (v));
    }

    volume       = raw (s, ids::masterVolume);
    clip         = raw (s, ids::masterClip);
    comp         = raw (s, ids::masterComp);
    accentParam  = raw (s, ids::accent);
    swing        = raw (s, ids::swing);
    groove       = raw (s, ids::groove);
    resolution   = raw (s, ids::resolution);
    pattern      = raw (s, ids::pattern);
    chainOn      = raw (s, ids::chainOn);
    for (int c = 0; c < kChainSlots; ++c)
        chain[(size_t) c] = raw (s, ids::chainSlot (c));
    tempoParam   = raw (s, ids::tempo);
    seed         = raw (s, ids::seed);
    midiOutParam = raw (s, ids::midiOut);
    seqOnParam   = raw (s, ids::sequencerOn);
}

void ParameterCache::readEngine (DrumEngine& engine) const noexcept
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& src = voices[(size_t) v];
        auto& dst = engine.voiceParams (v);
        for (int p = 0; p < numVoiceParams (v); ++p)
            dst.synth[(size_t) p] = load (src.synth[(size_t) p]);
        dst.levelDb     = load (src.level);
        dst.pan         = load (src.pan);
        dst.mute        = load (src.mute) > 0.5f;
        dst.solo        = load (src.solo) > 0.5f;
        dst.filter      = load (src.filter) * 0.01f;
        dst.drive       = load (src.drive) * 0.01f;
        dst.output      = (int) load (src.output);
        dst.sampleTune  = load (src.sampleTune);
        dst.sampleDecay = load (src.sampleDecay);
    }

    auto& m = engine.masterParams();
    m.volumeDb = load (volume);
    m.clip     = load (clip) * 0.01f;
    m.comp     = load (comp) * 0.01f;
    m.accent   = load (accentParam) * 0.01f;
}

void ParameterCache::readSequencer (SequencerSettings& s) const noexcept
{
    s.pattern      = (int) load (pattern) - 1;
    s.resolution   = (int) load (resolution);
    s.swing        = load (swing);
    s.groove       = (int) load (groove);
    s.chainEnabled = load (chainOn) > 0.5f;
    for (int c = 0; c < kChainSlots; ++c)
        s.chain[(size_t) c] = (int8_t) ((int) load (chain[(size_t) c]) - 1);
    s.seed         = (uint32_t) load (seed);
}

} // namespace fp
