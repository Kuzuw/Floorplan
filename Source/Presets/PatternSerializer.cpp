#include "Presets/PatternSerializer.h"

namespace fp::serial
{

namespace
{
int getInt (const juce::var& obj, const char* key, int fallback)
{
    const auto& v = obj[key];
    return (v.isInt() || v.isInt64() || v.isDouble()) ? (int) v : fallback;
}

bool getBool (const juce::var& obj, const char* key)
{
    const auto& v = obj[key];
    return v.isBool() ? (bool) v : (v.isInt() ? (int) v != 0 : false);
}
} // namespace

juce::var patternToVar (const Pattern& p)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "floorplan-pattern");
    root->setProperty ("version", kPatternFormatVersion);
    root->setProperty ("length", (int) p.length);

    auto* tracks = new juce::DynamicObject();
    for (int v = 0; v < kNumVoices; ++v)
    {
        juce::Array<juce::var> steps;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const auto& s = p.tracks[(size_t) v].steps[(size_t) i];
            if (! s.on)
                continue;

            // Only non-default fields are written to keep files small and readable.
            auto* st = new juce::DynamicObject();
            st->setProperty ("i", i);
            if (s.velocity != 100)    st->setProperty ("v", (int) s.velocity);
            if (s.probability != 100) st->setProperty ("p", (int) s.probability);
            if (s.micro != 0)         st->setProperty ("m", (int) s.micro);
            if (s.accent())           st->setProperty ("a", true);
            if (s.flam())             st->setProperty ("f", true);
            if (s.ratchet != 1)       st->setProperty ("r", (int) s.ratchet);
            if (s.tune != 0)          st->setProperty ("t", (int) s.tune);
            if (s.decay != 0)         st->setProperty ("d", (int) s.decay);
            steps.add (juce::var (st));
        }
        tracks->setProperty (kVoiceSpecs[(size_t) v].id, steps);
    }
    root->setProperty ("tracks", juce::var (tracks));
    return juce::var (root);
}

std::optional<Pattern> patternFromVar (const juce::var& root)
{
    if (! root.isObject() || root["format"].toString() != "floorplan-pattern")
        return std::nullopt;

    if (getInt (root, "version", 0) > kPatternFormatVersion)
        return std::nullopt; // written by a newer, incompatible version

    Pattern p;
    p.length = (uint8_t) juce::jlimit (1, kMaxSteps, getInt (root, "length", 16));

    const auto& tracks = root["tracks"];
    if (! tracks.isObject())
        return std::nullopt;

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& steps = tracks[kVoiceSpecs[(size_t) v].id];
        if (! steps.isArray())
            continue; // unknown or missing voices are simply empty

        for (const auto& st : *steps.getArray())
        {
            const int i = getInt (st, "i", -1);
            if (i < 0 || i >= kMaxSteps)
                continue;

            Step s;
            s.on = 1;
            s.velocity    = (uint8_t) juce::jlimit (1, 127, getInt (st, "v", 100));
            s.probability = (uint8_t) juce::jlimit (0, 100, getInt (st, "p", 100));
            s.micro       = (int8_t) juce::jlimit (-50, 50, getInt (st, "m", 0));
            s.ratchet     = (uint8_t) juce::jlimit (1, 4, getInt (st, "r", 1));
            s.tune        = (int8_t) juce::jlimit (-24, 24, getInt (st, "t", 0));
            s.decay       = (int8_t) juce::jlimit (-100, 100, getInt (st, "d", 0));
            s.setFlag (Step::Accent, getBool (st, "a"));
            s.setFlag (Step::Flam, getBool (st, "f"));
            p.tracks[(size_t) v].steps[(size_t) i] = s;
        }
    }

    return p;
}

juce::String patternToJson (const Pattern& p)
{
    return juce::JSON::toString (patternToVar (p), true);
}

std::optional<Pattern> patternFromJson (const juce::String& json)
{
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed())
        return std::nullopt;
    return patternFromVar (parsed);
}

juce::String bankToJson (const PatternBank& bank)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "floorplan-bank");
    root->setProperty ("version", kPatternFormatVersion);
    juce::Array<juce::var> patterns;
    for (const auto& p : bank.patterns)
        patterns.add (patternToVar (p));
    root->setProperty ("patterns", patterns);
    return juce::JSON::toString (juce::var (root), true);
}

std::optional<PatternBank> bankFromJson (const juce::String& json)
{
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed() || parsed["format"].toString() != "floorplan-bank")
        return std::nullopt;
    if (getInt (parsed, "version", 0) > kPatternFormatVersion)
        return std::nullopt;

    const auto& patterns = parsed["patterns"];
    if (! patterns.isArray())
        return std::nullopt;

    PatternBank bank;
    int index = 0;
    for (const auto& pv : *patterns.getArray())
    {
        if (index >= kNumPatterns)
            break;
        if (auto p = patternFromVar (pv))
            bank.patterns[(size_t) index] = *p;
        ++index;
    }
    return bank;
}

} // namespace fp::serial
