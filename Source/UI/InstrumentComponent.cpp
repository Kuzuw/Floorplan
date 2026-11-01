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
    drawSectionTitle (g, titleArea, "Voices");
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
                for (int i = 0; i < kNumSynthParams; ++i)
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
      muteButton (o.proc, "M", colours::mute),
      soloButton (o.proc, "S", colours::solo)
{
    muteButton.bind (ids::mute (v));
    soloButton.bind (ids::solo (v));
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    setRepaintsOnMouseActivity (true);
}

void InstrumentComponent::Row::resized()
{
    auto r = getLocalBounds().reduced (2, 3);
    soloButton.setBounds (r.removeFromRight (22));
    r.removeFromRight (3);
    muteButton.setBounds (r.removeFromRight (22));
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
    const int note = owner.proc.getVoiceNote (voice);
    if (flash > 0.0f || selected != shownSelected || sample != shownSample || note != shownNote)
    {
        shownSelected = selected;
        shownSample = sample;
        shownNote = note;
        repaint();
    }
}

void InstrumentComponent::Row::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool selected = owner.state.voice == voice;

    g.setColour (selected ? colours::raised.brighter (0.05f) : (isMouseOver() ? colours::raised : colours::panel));
    g.fillRect (r);
    g.setColour (selected ? colours::accent : colours::border);
    g.drawRect (r, 1.0f);
    if (selected)
        g.fillRect (r.withWidth (3.0f));

    // Trigger pad with activity flash.
    auto pad = getLocalBounds().reduced (8, 0).removeFromLeft (16).withSizeKeepingCentre (12, 12).toFloat();
    g.setColour (colours::well);
    g.fillRect (pad);
    if (flash > 0.0f)
    {
        g.setColour (colours::accentHot.withAlpha (flash));
        g.fillRect (pad);
    }
    g.setColour (colours::borderLight);
    g.drawRect (pad, 1.0f);

    auto text = getLocalBounds().withTrimmedLeft (30).withTrimmedRight (54);
    const auto& spec = kVoiceSpecs[(size_t) voice];
    g.setColour (selected ? colours::text : colours::textDim);
    g.setFont (font (12.5f, true));
    g.drawText (juce::String (spec.name).toUpperCase(), text, juce::Justification::centredLeft);

    // Small badge: sample-based voice or missing sample.
    if (shownSample)
    {
        const bool missing = owner.proc.isSampleMissing (voice);
        g.setColour (missing ? colours::mute : colours::accent.withAlpha (0.8f));
        g.setFont (font (9.0f, true));
        g.drawText (missing ? "MISSING" : "SMP", text, juce::Justification::centredRight);
    }
}

void InstrumentComponent::Row::mouseDown (const juce::MouseEvent& e)
{
    owner.state.selectVoice (voice);

    if (e.mods.isPopupMenu())
    {
        owner.showRowMenu (voice, *this);
        return;
    }

    // Clicking the pad (or anywhere with Alt) auditions the voice.
    if (e.x < 30 || e.mods.isAltDown())
        owner.proc.triggerVoice (voice, 0.85f);
}

} // namespace fp::ui
