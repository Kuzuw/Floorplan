#include "UI/HeaderComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

HeaderComponent::HeaderComponent (FloorplanProcessor& p) : proc (p), swingKnob (p, ids::swing, "Swing")
{
    addAndMakeVisible (presetBox);
    presetBox.setTooltip ("Kit preset (sound of all voices + master)");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != proc.presets.getCurrentIndex())
            proc.presets.load (idx);
    };

    for (auto* b : { &prevButton, &nextButton, &saveButton, &undoButton, &redoButton, &playButton, &menuButton, &advancedButton })
        addAndMakeVisible (b);

    prevButton.setTooltip ("Previous kit");
    nextButton.setTooltip ("Next kit");
    saveButton.setTooltip ("Save the current sound as a user kit");
    undoButton.setTooltip ("Undo pattern edit (Ctrl+Z)");
    redoButton.setTooltip ("Redo pattern edit (Ctrl+Y)");
    playButton.setTooltip ("Play the pattern on the internal clock (Space). In a DAW this previews while the host is stopped; "
                           "pressing play in the host ends the preview and the host takes over.");
    menuButton.setTooltip ("Patterns, kits, MIDI and shortcuts");
    advancedButton.setTooltip ("Advanced Mode: a wider sound panel with every control of the selected instrument");
    swingKnob.setTooltip ("Swing: delays every second step");

    prevButton.onClick = [this] { proc.presets.previous(); updatePresetList(); };
    nextButton.onClick = [this] { proc.presets.next(); updatePresetList(); };
    saveButton.onClick = [this] { savePreset(); };
    undoButton.onClick = [this] { if (proc.patterns.undo() && onUndoRedo) onUndoRedo(); };
    redoButton.onClick = [this] { if (proc.patterns.redo() && onUndoRedo) onUndoRedo(); };
    playButton.onClick = [this] { proc.setInternalPlaying (! proc.isInternalPlaying()); };
    menuButton.onClick = [this] { if (onMenu) onMenu(); };
    advancedButton.setClickingTogglesState (true);
    advancedButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    advancedButton.onClick = [this] { if (onAdvanced) onAdvanced (advancedButton.getToggleState()); };
    playButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);
    setKeyStyle (menuButton, KeyStyle::dark);

    swingKnob.setLayout (Knob::Layout::compact);
    swingKnob.setCap (colours::accent);
    addAndMakeVisible (swingKnob);
    addAndMakeVisible (bpm);
    updatePresetList();
}

void HeaderComponent::setAdvanced (bool advanced)
{
    advancedButton.setToggleState (advanced, juce::dontSendNotification);
}

void HeaderComponent::updatePresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto names = proc.presets.getNames();
    const int numFactory = proc.presets.getNumFactory();
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == numFactory)
            presetBox.addSeparator();
        presetBox.addItem ((i < numFactory ? "" : "User: ") + names[i], i + 1);
    }
    presetBox.setSelectedItemIndex (proc.presets.getCurrentIndex(), juce::dontSendNotification);
}

void HeaderComponent::savePreset()
{
    auto* window = new juce::AlertWindow ("Save Kit", "Name for the new user kit:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", proc.presets.getCurrentName(), {});
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<HeaderComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = window->getTextEditorContents ("name");
        if (! safe->proc.presets.saveUser (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Kit",
                                                    "Could not save the kit to\n" + PresetManager::getUserFolder().getFullPathName());
        safe->updatePresetList();
    }), true);
}

void HeaderComponent::refresh()
{
    undoButton.setEnabled (proc.patterns.canUndo());
    redoButton.setEnabled (proc.patterns.canRedo());

    const bool playing = proc.isInternalPlaying();
    if (playing != shownPlaying)
    {
        shownPlaying = playing;
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.setButtonText (playing ? "STOP" : "PLAY");
    }

    if (presetBox.getSelectedItemIndex() != proc.presets.getCurrentIndex())
        presetBox.setSelectedItemIndex (proc.presets.getCurrentIndex(), juce::dontSendNotification);

    // Display state: pattern.step readout, transport and clock source, or the
    // MIDI-learn prompt. Encoded as one string so it repaints only on change.
    const bool learn = proc.isLearning();
    const bool running = proc.isTransportRunning() && proc.getPlayingStep() >= 0;
    const bool seqOff = proc.apvts.getRawParameterValue (ids::sequencerOn)->load() < 0.5f;
    juce::String newStatus;
    newStatus << (running ? juce::String (proc.getPlayingPattern() + 1).paddedLeft ('0', 2) + "."
                                + juce::String (proc.getPlayingStep() + 1).paddedLeft ('0', 2)
                          : juce::String ("--.--"))
              << (proc.isHostSynced() ? "|HOST" : "|INT") << (running ? "|RUN" : "|STOP") << (learn ? "|LEARN" : "")
              << (seqOff ? "|OFF" : "");
    if (newStatus != status || learn != statusIsLearn)
    {
        status = newStatus;
        statusIsLearn = learn;
        repaint (lcdArea);
    }

    const int cpu = juce::roundToInt (proc.getCpuLoad() * 100.0f);
    if (cpu != shownCpu)
    {
        shownCpu = cpu;
        repaint (cpuArea);
    }

    bpm.refresh();
}

void HeaderComponent::resized()
{
    auto r = getLocalBounds().reduced (16, 9);
    r.removeFromBottom (1); // divider
    auto button = [] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), 30); };

    logoArea = r.removeFromLeft (150);
    r.removeFromLeft (12);
    prevButton.setBounds (button (r.removeFromLeft (28)));
    r.removeFromLeft (4);
    presetBox.setBounds (button (r.removeFromLeft (164)));
    r.removeFromLeft (4);
    nextButton.setBounds (button (r.removeFromLeft (28)));
    r.removeFromLeft (4);
    saveButton.setBounds (button (r.removeFromLeft (50)));
    r.removeFromLeft (14);
    undoButton.setBounds (button (r.removeFromLeft (50)));
    r.removeFromLeft (4);
    redoButton.setBounds (button (r.removeFromLeft (50)));
    r.removeFromLeft (14);

    menuButton.setBounds (button (r.removeFromRight (82)));
    r.removeFromRight (6);
    advancedButton.setBounds (button (r.removeFromRight (92)));
    r.removeFromRight (14);
    cpuArea = r.removeFromRight (38);
    r.removeFromRight (10);
    swingKnob.setBounds (r.removeFromRight (52));
    r.removeFromRight (6);
    playButton.setBounds (button (r.removeFromRight (64)));
    r.removeFromRight (12);

    lcdArea = r;
    bpm.setBounds (r.reduced (6, 5).removeFromRight (108));
}

void HeaderComponent::paint (juce::Graphics& g)
{
    g.setColour (colours::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // Wordmark: four stacked "floor" bars, the name and the model line.
    auto logo = logoArea;
    auto mark = logo.removeFromLeft (20).toFloat().withSizeKeepingCentre (20.0f, 26.0f);
    for (int i = 0; i < 4; ++i)
    {
        const float h = mark.getHeight() / 4.0f;
        g.setColour (i == 3 ? colours::accent : colours::ink);
        g.fillRect (mark.getX(), mark.getY() + (float) i * h + 0.75f, mark.getWidth() * (0.45f + 0.18f * (float) i), h - 1.5f);
    }
    logo.removeFromLeft (10);
    auto text = logo.withSizeKeepingCentre (logo.getWidth(), 36);
    g.setColour (colours::text);
    g.setFont (headlineFont (23.0f, true).withExtraKerningFactor (-0.01f));
    g.drawText ("FLOORPLAN", text.removeFromTop (22), juce::Justification::bottomLeft);
    g.setColour (colours::grey);
    g.setFont (labelFont (9.5f));
    g.drawText ("FP-1  DRUM COMPUTER", text, juce::Justification::topLeft);

    // Display: pattern.step, transport and clock source; BPM (child) on the right.
    const auto glass = drawScreen (g, lcdArea.toFloat());
    const auto fields = juce::StringArray::fromTokens (status, "|", {});
    auto left = glass.withRight ((float) bpm.getX() - 6.0f).reduced (6.0f, 3.0f);
    const auto unlit = colours::screenGhost;

    auto readout = left.removeFromLeft (70.0f);
    g.setColour (colours::screenDim);
    g.setFont (monoFont (9.0f, true));
    g.drawText ("PAT.STEP", readout.removeFromTop (10.0f), juce::Justification::centredLeft);
    drawSevenSegment (g, fields[0], readout.withTrimmedTop (3.0f), colours::screenText, juce::Justification::left);

    left.removeFromLeft (12.0f);
    const bool running = fields.contains ("RUN");
    juce::Path play;
    const auto playBox = left.removeFromLeft (11.0f).withSizeKeepingCentre (9.0f, 11.0f);
    play.addTriangle (playBox.getTopLeft(), playBox.getBottomLeft(), { playBox.getRight(), playBox.getCentreY() });
    g.setColour (running ? colours::accent : unlit);
    g.fillPath (play);

    left.removeFromLeft (9.0f);
    g.setFont (monoFont (10.0f, true));
    auto column = left.toNearestInt();
    if (statusIsLearn)
    {
        g.setColour (colours::accent);
        g.drawFittedText ("MIDI LEARN\nNOTE / CC", column, juce::Justification::centredLeft, 2, 0.8f);
    }
    else if (fields.contains ("OFF"))
    {
        // Internal sequencer switched off: only MIDI input and the pads play.
        g.setColour (colours::screenText);
        g.drawFittedText ("SEQ\nOFF", column, juce::Justification::centredLeft, 2, 0.8f);
    }
    else
    {
        // Clock source: both legends are printed, the active one is lit.
        const bool host = fields.contains ("HOST");
        const auto top = column.removeFromTop (column.getHeight() / 2);
        g.setColour (host ? colours::screenText : unlit);
        g.drawText ("HOST", top, juce::Justification::bottomLeft);
        g.setColour (host ? unlit : colours::screenText);
        g.drawText ("INT", column, juce::Justification::topLeft);
    }

    // CPU, printed on the body.
    auto cpu = cpuArea;
    g.setColour (colours::grey);
    g.setFont (labelFont (9.5f));
    g.drawText ("CPU", cpu.removeFromTop (cpu.getHeight() / 2), juce::Justification::bottomRight);
    g.setColour (shownCpu > 70 ? colours::danger : colours::text);
    g.setFont (monoFont (13.0f, true));
    g.drawText (juce::String (juce::jmax (0, shownCpu)) + "%", cpu, juce::Justification::topRight);
}

// ---------------------------------------------------------------------------
void HeaderComponent::BpmDisplay::refresh()
{
    const double b = proc.getDisplayBpm();
    const bool host = proc.isHostSynced();
    if (std::abs (b - shownBpm) > 0.01 || host != shownHost)
    {
        shownBpm = b;
        shownHost = host;
        repaint();
    }
}

void HeaderComponent::BpmDisplay::paint (juce::Graphics& g)
{
    // Drawn on the header's display: label above, segment digits below.
    auto area = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
    g.setColour (shownHost ? colours::accent : colours::screenDim);
    g.setFont (monoFont (9.0f, true));
    g.drawText (shownHost ? "BPM  HOST" : "BPM", area.removeFromTop (10.0f), juce::Justification::centredRight);

    const auto text = juce::String (shownBpm, 1).paddedLeft (' ', 5);
    drawSevenSegment (g, text, area.withTrimmedTop (3.0f).withTrimmedRight (2.0f), colours::screenText);
}

void HeaderComponent::BpmDisplay::mouseDown (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter (ids::tempo))
    {
        dragStartValue = p->convertFrom0to1 (p->getValue());
        p->beginChangeGesture();
    }
}

void HeaderComponent::BpmDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter (ids::tempo))
    {
        const float step = e.mods.isShiftDown() ? 0.02f : 0.25f;
        const float value = dragStartValue - (float) e.getDistanceFromDragStartY() * step;
        p->setValueNotifyingHost (p->convertTo0to1 (std::round (value * 10.0f) / 10.0f));
    }
}

void HeaderComponent::BpmDisplay::mouseUp (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter (ids::tempo))
        p->endChangeGesture();
}

void HeaderComponent::BpmDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* p = proc.apvts.getParameter (ids::tempo))
        p->setValueNotifyingHost (p->getDefaultValue());
}

void HeaderComponent::BpmDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (auto* p = proc.apvts.getParameter (ids::tempo))
    {
        const float value = p->convertFrom0to1 (p->getValue()) + (w.deltaY > 0 ? 1.0f : -1.0f);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

} // namespace fp::ui
