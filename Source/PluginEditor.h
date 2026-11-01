#pragma once

#include "PluginProcessor.h"
#include "UI/MainComponent.h"

namespace fp
{

// Resizable editor: the interface is laid out at a fixed design size and
// scaled uniformly, so text and controls stay proportional at any size and
// render sharply on high-DPI displays.
class FloorplanEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FloorplanEditor (FloorplanProcessor&);

    void resized() override;

private:
    ui::MainComponent main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloorplanEditor)
};

} // namespace fp
