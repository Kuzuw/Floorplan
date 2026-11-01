#include "UI/HeaderComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

HeaderComponent::HeaderComponent (FloorplanProcessor& p) : proc (p)
{
    addAndMakeVisible (presetBox);
    presetBox.setTooltip ("Kit preset (sound of all voices + master)");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != proc.presets.getCurrentIndex())
            proc.presets.load (idx);
    };

    for (auto* b : { &prevButton, &nextButton, &saveButton, &undoButton, &redoButton, &playButton, &menuButton })
        addAndMakeVisible (b);

    prevButton.setTooltip ("Previous kit");
    nextButton.setTooltip ("Next kit");
    saveButton.setTooltip ("Save the current sound as a user kit");
    undoButton.setTooltip ("Undo pattern edit (Ctrl+Z)");
    redoButton.setTooltip ("Redo pattern edit (Ctrl+Y)");
    playButton.setTooltip ("Internal transport (Space). In a DAW the host transport takes priority.");
    menuButton.setTooltip ("Patterns, MIDI and settings");

    prevButton.onClick = [this] { proc.presets.previous(); updatePresetList(); };
    nextButton.onClick = [this] { proc.presets.next(); updatePresetList(); };
    saveButton.onClick = [this] { savePreset(); };
    undoButton.onClick = [this] { if (proc.patterns.undo() && onUndoRedo) onUndoRedo(); };
    redoButton.onClick = [this] { if (proc.patterns.redo() && onUndoRedo) onUndoRedo(); };
    playButton.onClick = [this] { proc.setInternalPlaying (! proc.isInternalPlaying()); };
    menuButton.onClick = [this] { if (onMenu) onMenu(); };
    setKeyStyle (playButton, KeyStyle::orange);
    setKeyStyle (menuButton, KeyStyle::dark);

    addAndMakeVisible (bpm);
    updatePresetList();
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

    // Display state: pattern.step readout, transport and clock icons, or the
    // MIDI-learn prompt. Encoded as one string so it repaints only on change.
    const bool learn = proc.isLearning();
    const bool running = proc.isTransportRunning() && proc.getPlayingStep() >= 0;
    juce::String newStatus;
    newStatus << (running ? juce::String (proc.getPlayingPattern() + 1).paddedLeft ('0', 2) + "."
                                + juce::String (proc.getPlayingStep() + 1).paddedLeft ('0', 2)
                          : juce::String ("--.--"))
              << (proc.isHostSynced() ? "|HOST" : "|INT") << (running ? "|RUN" : "|STOP") << (learn ? "|LEARN" : "");
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

juce::Rectangle<int> HeaderComponent::plateArea() const
{
    // The display module plate, aligned with the plates below it.
    return getLocalBounds().withTrimmedTop (10).reduced (14, 0);
}

void HeaderComponent::resized()
{
    auto r = plateArea().reduced (18, 9);
    r.removeFromLeft (190); // wordmark

    auto presetArea = r.removeFromLeft (330);
    prevButton.setBounds (presetArea.removeFromLeft (30));
    presetArea.removeFromLeft (5);
    saveButton.setBounds (presetArea.removeFromRight (54));
    presetArea.removeFromRight (5);
    nextButton.setBounds (presetArea.removeFromRight (30));
    presetArea.removeFromRight (5);
    presetBox.setBounds (presetArea);

    r.removeFromLeft (14);
    undoButton.setBounds (r.removeFromLeft (54));
    r.removeFromLeft (5);
    redoButton.setBounds (r.removeFromLeft (54));

    menuButton.setBounds (r.removeFromRight (62));
    r.removeFromRight (10);
    cpuArea = r.removeFromRight (40);
    r.removeFromRight (10);
    playButton.setBounds (r.removeFromRight (74));
    r.removeFromRight (12);
    r.removeFromLeft (14);

    lcdArea = r;
    auto glass = r.reduced (5);
    bpm.setBounds (glass.removeFromRight (128));
}

void HeaderComponent::paint (juce::Graphics& g)
{
    // Display module plate (the chassis around it is painted by the parent).
    drawPanel (g, plateArea(), {});

    // Wordmark: four stacked "floor" bars, name, and the model line in orange.
    auto logo = plateArea().reduced (18, 9).removeFromLeft (184);
    auto mark = logo.removeFromLeft (22).toFloat().reduced (0.0f, 2.0f);
    for (int i = 0; i < 4; ++i)
    {
        const float h = mark.getHeight() / 4.0f;
        g.setColour (i == 3 ? colours::accent : colours::ink);
        g.fillRect (mark.getX(), mark.getY() + (float) i * h + 0.75f, mark.getWidth() * (0.45f + 0.18f * (float) i), h - 1.5f);
    }
    logo.removeFromLeft (10);
    g.setColour (colours::ink);
    g.setFont (font (23.0f, true).withExtraKerningFactor (-0.02f));
    g.drawText ("FLOORPLAN", logo.removeFromTop (24), juce::Justification::bottomLeft);
    g.setColour (colours::accentHot);
    g.setFont (font (10.5f, true).withExtraKerningFactor (0.08f));
    g.drawText ("DRUM COMPUTER  FP-1", logo, juce::Justification::topLeft);

    // Display: pattern.step on the left, transport icons, BPM (child) on the right.
    const auto glass = drawLcd (g, lcdArea.toFloat());
    const auto fields = juce::StringArray::fromTokens (status, "|", {});
    auto left = glass.withRight ((float) bpm.getX() - 6.0f).reduced (8.0f, 4.0f);
    const auto unlit = juce::Colours::white.withAlpha (0.1f); // printed-but-off LCD icons

    auto readout = left.removeFromLeft (74.0f);
    g.setColour (colours::lcdDim);
    g.setFont (monoFont (8.5f, true));
    g.drawText ("PAT.STEP", readout.removeFromTop (9.0f), juce::Justification::centredLeft);
    drawSevenSegment (g, fields[0], readout.withTrimmedTop (2.0f), colours::lcdText, juce::Justification::left);

    left.removeFromLeft (14.0f);
    const bool running = fields.contains ("RUN");
    juce::Path play;
    const auto playBox = left.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 12.0f);
    play.addTriangle (playBox.getTopLeft(), playBox.getBottomLeft(), { playBox.getRight(), playBox.getCentreY() });
    g.setColour (running ? colours::accent : unlit);
    g.fillPath (play);

    left.removeFromLeft (10.0f);
    g.setFont (monoFont (10.0f, true));
    auto column = left.toNearestInt();
    if (statusIsLearn)
    {
        g.setColour (colours::accent);
        g.drawFittedText ("MIDI LEARN\nNOTE / CC", column, juce::Justification::centredLeft, 2, 0.8f);
    }
    else
    {
        // Clock source: both legends are printed, the active one is lit.
        const bool host = fields.contains ("HOST");
        const auto top = column.removeFromTop (column.getHeight() / 2);
        g.setColour (host ? colours::lcdText : unlit);
        g.drawText ("HOST", top, juce::Justification::bottomLeft);
        g.setColour (host ? unlit : colours::lcdText);
        g.drawText ("INT", column, juce::Justification::topLeft);
    }

    // CPU, printed on the plate.
    auto cpu = cpuArea;
    g.setColour (colours::textDim);
    g.setFont (font (9.5f, true).withExtraKerningFactor (0.08f));
    g.drawText ("CPU", cpu.removeFromTop (cpu.getHeight() / 2), juce::Justification::bottomRight);
    g.setColour (shownCpu > 70 ? colours::mute : colours::text);
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
    // Drawn on the header's display glass: label above, segment digits below.
    auto area = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
    g.setColour (shownHost ? colours::accent : colours::lcdDim);
    g.setFont (monoFont (8.5f, true));
    g.drawText (shownHost ? "BPM  HOST" : "BPM", area.removeFromTop (9.0f), juce::Justification::centredRight);

    const auto text = juce::String (shownBpm, 1).paddedLeft (' ', 5);
    drawSevenSegment (g, text, area.withTrimmedTop (2.0f).withTrimmedRight (2.0f), colours::lcdText);
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
