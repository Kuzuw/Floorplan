#include "PluginEditor.h"

namespace fp
{

FloorplanEditor::FloorplanEditor (FloorplanProcessor& p) : AudioProcessorEditor (p), main (p)
{
    addAndMakeVisible (main);

    constexpr int w = ui::MainComponent::kDesignWidth, h = ui::MainComponent::kDesignHeight;
    setResizable (true, true);
    // Below 75% the smallest labels would drop under ~8 px.
    setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) w / (double) h);
    setSize (w * 9 / 10, h * 9 / 10);
}

void FloorplanEditor::resized()
{
    const float scale = (float) getWidth() / (float) ui::MainComponent::kDesignWidth;
    main.setTransform (juce::AffineTransform::scale (scale));
    main.setBounds (0, 0, ui::MainComponent::kDesignWidth, ui::MainComponent::kDesignHeight);
}

} // namespace fp
