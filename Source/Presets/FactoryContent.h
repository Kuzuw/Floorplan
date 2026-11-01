#pragma once

#include "Sequencer/Pattern.h"

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace fp::factory
{

struct NamedPattern
{
    juce::String name;
    Pattern pattern;
};

struct Kit
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values; // parameter id -> real value; others use defaults
};

const std::vector<NamedPattern>& patterns();
const std::vector<Kit>& kits();
PatternBank defaultBank();

} // namespace fp::factory
