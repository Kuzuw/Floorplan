#include "UI/InstrumentComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

InstrumentComponent::InstrumentComponent (FloorplanProcessor& p, UiState& s) : proc (p), state (s)
{
    for (int v = 0; v < kNumVoices; ++v)
        addAndMakeVisible (rows.add (new Row (*this, v)));
    state.addChangeListener (this);
}

InstrumentComponent::~InstrumentComponent()
{
    state.removeChangeListener (this);
}

void InstrumentComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* r : rows) r->refresh();
}

void InstrumentComponent::refresh()
{
    for (auto* r : rows) r->refresh();
}

juce::Rectangle<int> InstrumentComponent::rowBounds (int voice) const
{
    return { 0, gridTop + voice * rowHeight, getWidth(), rowHeight };
}

int InstrumentComponent::rowAt (juce::Point<int> pos) const
{
    for (int v = 0; v < kNumVoices; ++v)
        if (rowBounds (v).contains (pos))
            return v;
    return -1;
}

void InstrumentComponent::resized()
{
    for (int v = 0; v < kNumVoices; ++v)
        rows[v]->setBounds (rowBounds (v).reduced (0, 1));
}

void InstrumentComponent::paint (juce::Graphics& g)
{
    // Align the title with the sequencer's toolbar row.
    const auto titleArea = getLocalBounds().withHeight (gridTop).reduced (0, juce::jmax (0, (gridTop - 26) / 2));
    drawSectionTitle (g, titleArea, "Tracks");
}

void InstrumentComponent::showRowMenu (int voice, Row& row)
{
    const auto& spec = kVoiceSpecs[(size_t) voice];
    const bool hasSample = proc.getSamplePath (voice).isNotEmpty();
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (spec.name).toUpperCase());
    menu.addItem (1, "Load sample...");
    menu.addItem (2, "Clear sample (use synthesis)", hasSample);
    menu.addSeparator();
    menu.addItem (3, "Learn MIDI note (currently " + juce::MidiMessage::getMidiNoteName (proc.getVoiceNote (voice), true, true, 3) + ")");
    menu.addSeparator();
    menu.addItem (4, "Copy track");
    menu.addItem (5, "Paste track", state.trackClipboard.has_value());
    menu.addItem (6, "Clear track");
    menu.addItem (7, "Randomize track");
    menu.addItem (8, "Randomize sound");
    menu.addItem (9, "Reset sound to defaults");

    juce::Component::SafePointer<InstrumentComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row), [safe, voice] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& proc = safe->proc;
        auto& state = safe->state;
        const int pattern = (int) proc.apvts.getRawParameterValue (ids::pattern)->load() - 1;

        switch (result)
        {
            case 1: if (safe->onLoadSample) safe->onLoadSample (voice); break;
            case 2: proc.clearSample (voice); break;
            case 3: proc.beginNoteLearn (voice); break;
            case 4: state.trackClipboard = proc.patterns.getPattern (pattern).tracks[(size_t) voice]; break;
            case 5:
                if (state.trackClipboard)
                {
                    const Track t = *state.trackClipboard;
                    proc.patterns.edit (pattern, [voice, t] (Pattern& p) { p.tracks[(size_t) voice] = t; });
                }
                break;
            case 6: proc.patterns.edit (pattern, [voice] (Pattern& p) { p.clearTrack (voice); }); break;
            case 7:
            {
                const float amount = proc.apvts.getRawParameterValue (ids::randAmount)->load() * 0.01f;
                const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
                proc.patterns.edit (pattern, [voice, amount, seed] (Pattern& p) { p.randomiseTrack (voice, amount, seed); });
                break;
            }
            case 8:
            case 9:
            {
                auto& rng = juce::Random::getSystemRandom();
                const float amount = proc.apvts.getRawParameterValue (ids::randAmount)->load() * 0.01f;
                // Reset covers every synthesis parameter; randomizing stays on
                // the main six so it cannot reach for extreme Advanced settings.
                const int count = result == 9 ? numVoiceParams (voice) : kNumSynthParams;
                for (int i = 0; i < count; ++i)
                    if (auto* param = proc.apvts.getParameter (ids::synth (voice, i)))
                    {
                        // Randomize around the current value, scaled by the intensity.
                        const float current = param->getValue();
                        const float target = result == 9 ? param->getDefaultValue()
                                                         : juce::jlimit (0.0f, 1.0f, current + (rng.nextFloat() - 0.5f) * amount);
                        param->setValueNotifyingHost (target);
                    }
                break;
            }
            default: break;
        }

        if (result >= 4 && result <= 7 && safe->onPatternEdited)
            safe->onPatternEdited();
    });
}

// ---------------------------------------------------------------------------
InstrumentComponent::Row::Row (InstrumentComponent& o, int v)
    : owner (o), voice (v),
      muteButton (o.proc, "M", colours::ink),
      soloButton (o.proc, "S", colours::accent)
{
    muteButton.bind (ids::mute (v));
    soloButton.bind (ids::solo (v));
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    setRepaintsOnMouseActivity (true);
    setTooltip ("Click to select. Click the badge to audition, right-click for samples, MIDI note and track tools.");
}

juce::Rectangle<float> InstrumentComponent::Row::badgeArea() const
{
    const auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
    return { r.getX() + 5.0f, r.getY() + 5.0f, 26.0f, r.getHeight() - 10.0f };
}

void InstrumentComponent::Row::resized()
{
    auto r = getLocalBounds().reduced (6, 2);
    r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight() - 8, 20));
    soloButton.setBounds (r.removeFromRight (20));
    r.removeFromRight (4);
    muteButton.setBounds (r.removeFromRight (20));
}

void InstrumentComponent::Row::refresh()
{
    const uint32_t count = owner.proc.getTriggerCount (voice);
    if (count != lastCount)
    {
        lastCount = count;
        flash = 1.0f;
    }
    else if (flash > 0.0f)
    {
        flash = flash < 0.05f ? 0.0f : flash * 0.75f;
    }

    const bool selected = owner.state.voice == voice;
    const bool sample = owner.proc.getSamplePath (voice).isNotEmpty();
    const bool muted = owner.proc.apvts.getRawParameterValue (ids::mute (voice))->load() > 0.5f;
    const int note = owner.proc.getVoiceNote (voice);
    if (flash > 0.0f || selected != shownSelected || sample != shownSample || muted != shownMuted || note != shownNote)
    {
        shownSelected = selected;
        shownSample = sample;
        shownMuted = muted;
        shownNote = note;
        repaint();
    }
}

void InstrumentComponent::Row::paint (juce::Graphics& g)
{
    // A module: outlined when idle, filled with ink when selected.
    const auto r = getLocalBounds().toFloat().reduced (0.75f, 2.0f);
    const bool selected = owner.state.voice == voice;
    g.setColour (selected ? colours::ink : (isMouseOver() ? colours::hover : colours::paper));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (selected ? colours::ink : colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    drawBadge (g, badgeArea(), voice);

    const auto& spec = kVoiceSpecs[(size_t) voice];
    auto text = getLocalBounds().withLeft ((int) badgeArea().getRight() + 8).withRight (muteButton.getX() - 14);
    const auto nameColour = selected ? colours::paper : colours::ink;
    g.setColour (shownMuted ? (selected ? colours::grey : colours::greyLight) : nameColour);
    g.setFont (font (11.5f, true).withExtraKerningFactor (0.04f));
    g.drawFittedText (juce::String (spec.name).toUpperCase(), text, juce::Justification::centredLeft, 1, 0.8f);

    // Sample tag under the name: sample-based voice or missing file.
    if (shownSample)
    {
        const bool missing = owner.proc.isSampleMissing (voice);
        g.setColour (missing ? colours::danger : (selected ? colours::accent : colours::accentText));
        g.setFont (labelFont (8.5f));
        g.drawText (missing ? "MISSING" : "SAMPLE", text.withTrimmedTop (text.getHeight() / 2 + 5), juce::Justification::topLeft);
    }

    // Activity dot: lights on each hit.
    drawLed (g, { (float) muteButton.getX() - 8.0f, r.getCentreY() }, 3.0f, colours::accent, flash);
}

void InstrumentComponent::Row::mouseDown (const juce::MouseEvent& e)
{
    owner.state.selectVoice (voice);

    if (e.mods.isPopupMenu())
    {
        owner.showRowMenu (voice, *this);
        return;
    }

    // Clicking the badge (or anywhere with Alt) auditions the voice.
    if (badgeArea().contains (e.position) || e.mods.isAltDown())
        owner.proc.triggerVoice (voice, 0.85f);
}

} // namespace fp::ui
