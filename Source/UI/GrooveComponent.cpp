#include "UI/GrooveComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

GrooveComponent::GrooveComponent (FloorplanProcessor& p)
    : proc (p),
      swingKnob (p, ids::swing, "Swing"),
      accentKnob (p, ids::accent, "Accent"),
      randomKnob (p, ids::randAmount, "Rand Amt"),
      seedKnob (p, ids::seed, "Variation"),
      grooveBox (p, ids::groove),
      resolutionBox (p, ids::resolution),
      chainButton (p, "CHAIN", colours::accent)
{
    for (auto* k : { &swingKnob, &accentKnob, &randomKnob, &seedKnob })
        addAndMakeVisible (k);
    swingKnob.setCap (colours::accent);   // the feel control gets the signal colour
    accentKnob.setCap (colours::keyDark);
    setKeyStyle (chainButton, KeyStyle::light);

    grooveBox.setTooltip ("Groove template (timing feel)");
    resolutionBox.setTooltip ("Step resolution");
    addAndMakeVisible (grooveBox);
    addAndMakeVisible (resolutionBox);

    rotateLeft.setTooltip ("Rotate the selected track one step earlier");
    rotateRight.setTooltip ("Rotate the selected track one step later");
    rotateLeft.onClick = [this] { if (onRotate) onRotate (-1); };
    rotateRight.onClick = [this] { if (onRotate) onRotate (1); };
    addAndMakeVisible (rotateLeft);
    addAndMakeVisible (rotateRight);

    chainButton.bind (ids::chainOn);
    chainButton.setTooltip ("Play the chain slots in order instead of the selected pattern");
    addAndMakeVisible (chainButton);

    for (int c = 0; c < kChainSlots; ++c)
    {
        auto* box = chainSlots.add (new ParamCombo (p, ids::chainSlot (c)));
        box->setTooltip ("Chain slot " + juce::String (c + 1));
        addAndMakeVisible (box);
    }
}

void GrooveComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (22);

    auto knobs = r.removeFromTop (78);
    const int kw = knobs.getWidth() / 4;
    for (auto* k : { &swingKnob, &accentKnob, &randomKnob, &seedKnob })
        k->setBounds (knobs.removeFromLeft (kw).reduced (3, 0));

    r.removeFromTop (20);
    auto combos = r.removeFromTop (24);
    const int half = (combos.getWidth() - 6) / 2;
    grooveBox.setBounds (combos.removeFromLeft (half));
    combos.removeFromLeft (6);
    resolutionBox.setBounds (combos);

    r.removeFromTop (14);
    auto rotate = r.removeFromTop (24);
    rotate.removeFromLeft (70);
    rotateLeft.setBounds (rotate.removeFromLeft (32));
    rotate.removeFromLeft (4);
    rotateRight.setBounds (rotate.removeFromLeft (32));

    r.removeFromTop (12);
    auto chainRow = r.removeFromTop (22);
    chainButton.setBounds (chainRow.removeFromLeft (62));
    r.removeFromTop (6);
    auto slotsRow = r.removeFromTop (24);
    const int sw = slotsRow.getWidth() / kChainSlots;
    for (auto* s : chainSlots)
        s->setBounds (slotsRow.removeFromLeft (sw).reduced (1, 0));
}

void GrooveComponent::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Groove & Pattern");

    g.setColour (colours::textDim);
    g.setFont (font (9.5f, true).withExtraKerningFactor (0.06f));
    auto label = [&g] (juce::Rectangle<int> area, const juce::String& text)
    { g.drawText (text, area, juce::Justification::centredLeft); };

    label ({ grooveBox.getX(), grooveBox.getY() - 14, 100, 12 }, "GROOVE");
    label ({ resolutionBox.getX(), resolutionBox.getY() - 14, 100, 12 }, "RESOLUTION");
    label ({ grooveBox.getX(), rotateLeft.getY(), 70, rotateLeft.getHeight() }, "ROTATE");
    label ({ chainButton.getRight() + 8, chainButton.getY(), 160, chainButton.getHeight() }, "PATTERN CHAIN");
}

} // namespace fp::ui
