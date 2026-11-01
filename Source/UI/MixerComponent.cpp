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
    meter.setBounds (r.removeFromRight (7).reduced (0, 5));
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
      compKnob (p, ids::masterComp, "Comp"),
      characterToggle (p, "ON", colours::accent),
      colorKnob (p, ids::charColor, "Color"),
      punchKnob (p, ids::charPunch, "Punch", true),
      varyKnob (p, ids::charVary, "Vary"),
      velToneKnob (p, ids::charVelTone, "Vel Tone")
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

    for (auto* b : { &mixerTab, &characterTab })
    {
        b->setRadioGroupId (5151);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    mixerTab.setToggleState (true, juce::dontSendNotification);
    mixerTab.onClick = [this] { showPage (false); };
    characterTab.onClick = [this] { showPage (true); };

    characterToggle.bind (ids::charOn);
    characterToggle.setTooltip ("Character section on/off. Off is clean and bit-identical to having no section");
    colorKnob.setTooltip ("Color: parallel asymmetric saturation, level-matched at -10 dBFS (0 = clean)");
    punchKnob.setTooltip ("Punch: transient shaper. Right boosts attacks, left softens them (up to 6 dB)");
    varyKnob.setTooltip ("Vary: small per-hit differences in tune, decay, level and tone. "
                         "Reproducible: the Groove VARIATION seed picks the sequence");
    velToneKnob.setTooltip ("Velocity Tone: how much softer hits also sound duller");
    for (auto* c : std::initializer_list<juce::Component*> { &characterToggle, &colorKnob, &punchKnob, &varyKnob, &velToneKnob })
        addChildComponent (c);

    state.addChangeListener (this);
}

bool MixerComponent::characterOn() const
{
    return proc.apvts.getRawParameterValue (ids::charOn)->load() > 0.5f;
}

void MixerComponent::showPage (bool character)
{
    characterPage = character;
    for (auto* s : strips)
        s->setVisible (! character);
    for (auto* c : std::initializer_list<juce::Component*> { &characterToggle, &colorKnob, &punchKnob, &varyKnob, &velToneKnob })
        c->setVisible (character);
    repaint();
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
    if (characterOn() != shownCharacterOn)
    {
        shownCharacterOn = ! shownCharacterOn;
        repaint();
    }
}

void MixerComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto title = r.removeFromTop (24);
    characterTab.setBounds (title.removeFromRight (92).withHeight (20));
    title.removeFromRight (4);
    mixerTab.setBounds (title.removeFromRight (64).withHeight (20));

    auto master = r.removeFromRight (120);
    r.removeFromRight (12);

    // Character page: one row of knobs, then the switch beside the help text.
    auto page = r;
    auto knobRow = page.removeFromTop (juce::jmin (page.getHeight(), 92));
    const int cellW = juce::jmin (84, knobRow.getWidth() / 4);
    for (auto* k : { &colorKnob, &punchKnob, &varyKnob, &velToneKnob })
        k->setBounds (knobRow.removeFromLeft (cellW).reduced (3, 0));
    page.removeFromTop (12);
    characterToggle.setBounds (page.removeFromLeft (48).withHeight (26));
    page.removeFromLeft (12);
    characterHelp = page;

    const int w = r.getWidth() / kNumVoices;
    for (auto* s : strips)
        s->setBounds (r.removeFromLeft (w).reduced (1, 0));

    master.removeFromBottom (26);
    auto knobs = master.removeFromLeft (54);
    clipKnob.setBounds (knobs.removeFromTop (88));
    knobs.removeFromTop (8);
    compKnob.setBounds (knobs.removeFromTop (88));
    master.removeFromLeft (8);
    masterMeter.setBounds (master.removeFromRight (30).reduced (0, 5));
    master.removeFromRight (2);
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
    auto label = masterArea.removeFromBottom (22);
    g.drawText ("MASTER", label, juce::Justification::centred);

    // The Character section is on: a lit dot beside MASTER, visible on both pages.
    if (characterOn())
    {
        g.setColour (colours::accent);
        g.fillEllipse ((float) label.getCentreX() - 37.0f, (float) label.getCentreY() - 3.0f, 6.0f, 6.0f);
    }

    if (characterPage && ! characterHelp.isEmpty())
    {
        g.setColour (colours::inkSoft);
        g.setFont (labelFont (10.5f));
        g.drawFittedText ("Color adds harmonics and rounds peaks. Punch shapes attacks. Vary makes each hit slightly "
                          "different, the same on every play. Vel Tone makes soft hits duller. Color and Punch: main bus only.",
                          characterHelp, juce::Justification::topLeft, 5);
    }
}

} // namespace fp::ui
