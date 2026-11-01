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
      pan (p, ids::pan (v), "Pan", true),
      mute (p, "M", colours::mute),
      solo (p, "S", colours::solo)
{
    setupFader (fader);
    faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids::level (v), fader);
    if (auto* param = p.apvts.getParameter (ids::level (v)))
        fader.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    fader.setTooltip (juce::String (kVoiceSpecs[(size_t) v].name) + " level");

    mute.bind (ids::mute (v));
    solo.bind (ids::solo (v));
    for (auto* c : std::initializer_list<juce::Component*> { &fader, &pan, &meter, &mute, &solo })
        addAndMakeVisible (c);
}

void MixerComponent::Strip::resized()
{
    auto r = getLocalBounds().reduced (2, 0);
    r.removeFromBottom (16); // name
    auto buttons = r.removeFromBottom (20);
    mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
    solo.setBounds (buttons.reduced (1, 0));
    r.removeFromBottom (4);
    pan.setBounds (r.removeFromTop (46));
    r.removeFromTop (2);
    meter.setBounds (r.removeFromRight (6).reduced (0, 4));
    fader.setBounds (r);
}

void MixerComponent::Strip::paint (juce::Graphics& g)
{
    const bool selected = state.voice == voice;
    if (selected)
    {
        g.setColour (colours::raised.withAlpha (0.7f));
        g.fillRect (getLocalBounds());
    }
    g.setColour (selected ? colours::accent : colours::textDim);
    g.setFont (font (10.5f, true));
    g.drawText (kVoiceSpecs[(size_t) voice].shortName, getLocalBounds().removeFromBottom (16), juce::Justification::centred);
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
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (22);

    auto master = r.removeFromRight (124);
    r.removeFromRight (10);
    const int w = r.getWidth() / kNumVoices;
    for (auto* s : strips)
        s->setBounds (r.removeFromLeft (w).reduced (1, 0));

    master.removeFromBottom (16);
    auto knobs = master.removeFromLeft (52);
    clipKnob.setBounds (knobs.removeFromTop (60));
    knobs.removeFromTop (6);
    compKnob.setBounds (knobs.removeFromTop (60));
    master.removeFromLeft (8);
    masterMeter.setBounds (master.removeFromRight (16).reduced (0, 4));
    master.removeFromRight (4);
    masterFader.setBounds (master);
}

void MixerComponent::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Mixer");

    auto masterArea = getLocalBounds().reduced (10, 8).removeFromRight (124);
    g.setColour (colours::border);
    g.fillRect (masterArea.getX() - 6, masterArea.getY() + 22, 1, masterArea.getHeight() - 22);
    g.setColour (colours::accent);
    g.setFont (font (10.5f, true));
    g.drawText ("MASTER", masterArea.removeFromBottom (16), juce::Justification::centred);
}

} // namespace fp::ui
