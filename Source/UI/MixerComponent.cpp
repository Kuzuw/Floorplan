#include "UI/MixerComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

namespace
{
void setupFader (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearVertical);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (true, true, nullptr);
}
} // namespace

MixerComponent::Strip::Strip (FloorplanProcessor& p, UiState& s, int v)
    : proc (p), state (s), voice (v),
      pan (p, ids::pan (v), "Pan", true)
{
    setupFader (fader);
    faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids::level (v), fader);
    if (auto* param = p.apvts.getParameter (ids::level (v)))
        fader.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    fader.setTooltip (juce::String (kVoiceSpecs[(size_t) v].name) + " level (double-click resets)");
    pan.setLayout (Knob::Layout::compact);

    for (auto* c : std::initializer_list<juce::Component*> { &fader, &pan, &meter })
        addAndMakeVisible (c);
}

void MixerComponent::Strip::resized()
{
    auto r = getLocalBounds().reduced (2, 0);
    r.removeFromBottom (22); // badge
    r.removeFromBottom (4);
    pan.setBounds (r.removeFromTop (46));
    r.removeFromTop (4);
    meter.setBounds (r.removeFromRight (4).reduced (0, 5));
    fader.setBounds (r);
}

void MixerComponent::Strip::paint (juce::Graphics& g)
{
    // Selected channel: outlined in ink, like its instrument module.
    const bool selected = state.voice == voice;
    if (selected)
    {
        g.setColour (colours::ink);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), 5.0f, 1.5f);
    }
    drawBadge (g, getLocalBounds().removeFromBottom (22).toFloat().reduced (5.0f, 3.0f), voice);
}

// ---------------------------------------------------------------------------
MixerComponent::MixerComponent (FloorplanProcessor& p, UiState& s)
    : proc (p), state (s),
      clipKnob (p, ids::masterClip, "Clip"),
      compKnob (p, ids::masterComp, "Comp")
{
    for (int v = 0; v < kNumVoices; ++v)
        addAndMakeVisible (strips.add (new Strip (p, s, v)));

    setupFader (masterFader);
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids::masterVolume, masterFader);
    masterFader.setDoubleClickReturnValue (true, 0.0);
    masterFader.setTooltip ("Master volume");
    clipKnob.setTooltip ("Master soft clip / saturation (0 = off)");
    compKnob.setTooltip ("Master bus compressor amount (0 = off)");

    addAndMakeVisible (masterFader);
    addAndMakeVisible (clipKnob);
    addAndMakeVisible (compKnob);
    addAndMakeVisible (masterMeter);
    state.addChangeListener (this);
}

MixerComponent::~MixerComponent()
{
    state.removeChangeListener (this);
}

void MixerComponent::refresh()
{
    for (int v = 0; v < kNumVoices; ++v)
        strips[v]->meter.setLevels (proc.getVoicePeak (v));
    masterMeter.setLevels (proc.getMasterPeak (0), proc.getMasterPeak (1));
}

void MixerComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (24);

    auto master = r.removeFromRight (120);
    r.removeFromRight (12);
    const int w = r.getWidth() / kNumVoices;
    for (auto* s : strips)
        s->setBounds (r.removeFromLeft (w).reduced (1, 0));

    master.removeFromBottom (26);
    auto knobs = master.removeFromLeft (54);
    clipKnob.setBounds (knobs.removeFromTop (88));
    knobs.removeFromTop (8);
    compKnob.setBounds (knobs.removeFromTop (88));
    master.removeFromLeft (8);
    masterMeter.setBounds (master.removeFromRight (12).reduced (0, 5));
    master.removeFromRight (4);
    masterFader.setBounds (master);
}

void MixerComponent::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Mixer");

    auto masterArea = getLocalBounds().reduced (12, 10).removeFromRight (120);
    g.setColour (colours::line);
    g.fillRect (masterArea.getX() - 6, masterArea.getY() + 24, 1, masterArea.getHeight() - 24);
    g.setColour (colours::ink);
    g.setFont (labelFont (11.0f));
    g.drawText ("MASTER", masterArea.removeFromBottom (22), juce::Justification::centred);
}

} // namespace fp::ui
