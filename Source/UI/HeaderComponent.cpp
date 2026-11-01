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
    playButton.setColour (juce::TextButton::buttonOnColourId, colours::accent);

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

    // Centre status: transport position, or the MIDI-learn prompt.
    juce::String newStatus;
    const bool learn = proc.isLearning();
    if (learn)
    {
        newStatus = "MIDI LEARN  -  send a note or move a controller";
    }
    else
    {
        const int step = proc.getPlayingStep();
        const int pat = proc.getPlayingPattern();
        newStatus = proc.isTransportRunning() && step >= 0
                        ? "PATTERN " + juce::String (pat + 1).paddedLeft ('0', 2) + "  /  STEP " + juce::String (step + 1).paddedLeft ('0', 2)
                        : juce::String ("STOPPED");
        newStatus << "\n" << (proc.isHostSynced() ? "HOST SYNC" : "INTERNAL CLOCK");
    }
    if (newStatus != status || learn != statusIsLearn)
    {
        status = newStatus;
        statusIsLearn = learn;
        repaint (statusArea);
    }

    const int cpu = juce::roundToInt (proc.getCpuLoad() * 100.0f);
    if (cpu != shownCpu)
    {
        shownCpu = cpu;
        repaint (getLocalBounds().removeFromRight (250));
    }

    bpm.refresh();
}

void HeaderComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 9);
    r.removeFromLeft (190); // logo

    auto presetArea = r.removeFromLeft (360);
    prevButton.setBounds (presetArea.removeFromLeft (28));
    presetArea.removeFromLeft (4);
    saveButton.setBounds (presetArea.removeFromRight (56));
    presetArea.removeFromRight (4);
    nextButton.setBounds (presetArea.removeFromRight (28));
    presetArea.removeFromRight (4);
    presetBox.setBounds (presetArea);

    r.removeFromLeft (16);
    undoButton.setBounds (r.removeFromLeft (56));
    r.removeFromLeft (4);
    redoButton.setBounds (r.removeFromLeft (56));

    menuButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (100); // CPU readout
    r.removeFromRight (8);
    bpm.setBounds (r.removeFromRight (120));
    r.removeFromRight (8);
    playButton.setBounds (r.removeFromRight (70));
    statusArea = r.reduced (12, 0);
}

void HeaderComponent::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::border);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // Logo: four stacked "floor" bars + wordmark.
    auto logo = getLocalBounds().reduced (14, 12).removeFromLeft (180);
    auto mark = logo.removeFromLeft (26).toFloat();
    for (int i = 0; i < 4; ++i)
    {
        const float h = mark.getHeight() / 4.0f;
        g.setColour (i == 3 ? colours::accent : colours::textDim.withAlpha (0.35f + 0.15f * (float) i));
        g.fillRoundedRectangle (mark.getX(), mark.getY() + (float) i * h + 1.0f, mark.getWidth() * (0.55f + 0.15f * (float) i), h - 2.5f, 1.5f);
    }
    logo.removeFromLeft (10);
    g.setColour (colours::text);
    g.setFont (font (20.0f, true));
    g.drawText ("FLOORPLAN", logo.removeFromTop (20), juce::Justification::bottomLeft);
    g.setColour (colours::textFaint);
    g.setFont (font (10.0f, true));
    g.drawText ("HOUSE / EDM DRUM MACHINE", logo, juce::Justification::topLeft);

    // Status readout
    g.setColour (colours::well);
    g.fillRoundedRectangle (statusArea.toFloat(), 3.0f);
    g.setColour (statusIsLearn ? colours::accent : colours::textDim);
    g.setFont (font (10.5f, true));
    g.drawFittedText (status, statusArea.reduced (8, 2), juce::Justification::centred, 2, 0.8f);

    // CPU readout
    auto cpuArea = getLocalBounds().reduced (12, 9);
    cpuArea.removeFromRight (64);
    cpuArea = cpuArea.removeFromRight (100);
    g.setColour (colours::textFaint);
    g.setFont (font (10.0f, true));
    g.drawText ("CPU", cpuArea.removeFromTop (cpuArea.getHeight() / 2), juce::Justification::bottomRight);
    g.setColour (shownCpu > 70 ? colours::meterHot : colours::textDim);
    g.setFont (font (13.0f, true));
    g.drawText (juce::String (juce::jmax (0, shownCpu)) + "%", cpuArea, juce::Justification::topRight);
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
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colours::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    auto area = getLocalBounds().reduced (8, 2);
    g.setColour (colours::text);
    g.setFont (font (18.0f, true));
    g.drawText (juce::String (shownBpm, 1), area.removeFromLeft (area.getWidth() - 34), juce::Justification::centredRight);
    g.setColour (shownHost ? colours::accent : colours::textFaint);
    g.setFont (font (9.5f, true));
    g.drawText (shownHost ? "HOST" : "BPM", area, juce::Justification::centredRight);
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
