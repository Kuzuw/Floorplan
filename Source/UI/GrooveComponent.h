#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Groove and pattern section: groove template, step resolution, accent,
// randomization, track rotation and pattern chaining. (Swing is in the top bar.)
class GrooveComponent final : public juce::Component
{
public:
    explicit GrooveComponent (FloorplanProcessor& p);

    std::function<void (int)> onRotate;   // rotate selected track by +-1

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FloorplanProcessor& proc;
    Knob accentKnob, randomKnob, seedKnob;
    ParamCombo grooveBox, resolutionBox;
    juce::TextButton rotateLeft { "<" }, rotateRight { ">" };
    ParamToggle chainButton;
    juce::OwnedArray<ParamCombo> chainSlots;
};

} // namespace fp::ui
