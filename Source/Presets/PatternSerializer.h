#pragma once

#include "Sequencer/Pattern.h"

#include <juce_core/juce_core.h>

#include <optional>

// JSON import/export of patterns and banks. The format is documented in
// docs/PATTERN_FORMAT.md; tracks are keyed by voice ID so files stay valid
// when voices are added in the future.
namespace fp::serial
{

inline constexpr int kPatternFormatVersion = 1;

juce::var patternToVar (const Pattern& p);
std::optional<Pattern> patternFromVar (const juce::var& v);

juce::String patternToJson (const Pattern& p);
std::optional<Pattern> patternFromJson (const juce::String& json);

juce::String bankToJson (const PatternBank& bank);
std::optional<PatternBank> bankFromJson (const juce::String& json);

} // namespace fp::serial
