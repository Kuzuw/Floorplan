#include "UI/MainComponent.h"
#include "Parameters/ParameterIDs.h"
#include "Presets/FactoryContent.h"
#include "Presets/PatternSerializer.h"

namespace fp::ui
{

namespace
{
// Editor mode, stored on the parameter state tree so it is saved with the
// project. It is not a parameter: it never affects the sound or automation.
const juce::Identifier kAdvancedProperty { "uiAdvancedMode" };
} // namespace

MainComponent::MainComponent (FloorplanProcessor& p) : proc (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &instruments, &sequencer, &sound, &groove, &mixer })
        addAndMakeVisible (c);

    advanced = (bool) proc.apvts.state.getProperty (kAdvancedProperty, false);
    header.setAdvanced (advanced);
    sound.setAdvanced (advanced);
    header.onAdvanced = [this] (bool a)
    {
        proc.apvts.state.setProperty (kAdvancedProperty, a, nullptr);
        setAdvanced (a);
    };
    header.onMenu = [this] { showSettingsMenu(); };
    header.onUndoRedo = [this] { sequencer.patternChanged(); };
    instruments.onPatternEdited = [this] { patternEdited(); };
    instruments.onLoadSample = [this] (int v) { chooseSample (v); };
    sound.onLoadSample = [this] (int v) { chooseSample (v); };
    groove.onRotate = [this] (int amount) { sequencer.rotateTrack (amount); };

    setSize (kDesignWidth, kDesignHeight);
    timerCallback(); // show live values immediately, not one tick later
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainComponent::setAdvanced (bool shouldBeAdvanced)
{
    if (advanced == shouldBeAdvanced)
        return;
    advanced = shouldBeAdvanced;
    header.setAdvanced (advanced);
    sound.setAdvanced (advanced);
    resized();
    repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Sequencer section: one surface for the track modules and the step grid.
    drawPanel (g, sequencerPanel, {});
    g.setColour (colours::line);
    g.fillRect (instruments.getRight() + 7, sequencerPanel.getY() + 12, 1, sequencerPanel.getHeight() - 24);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (60));
    r.reduce (16, 12);

    auto bottom = r.removeFromBottom (270);
    r.removeFromBottom (12);

    sequencerPanel = r;
    auto top = r.reduced (12, 10);
    auto instrumentArea = top.removeFromLeft (184);
    top.removeFromLeft (14);
    sequencer.setBounds (top);
    instruments.setBounds (instrumentArea);
    instruments.setRowHeight (sequencer.getY() + sequencer.getGridTop() - instruments.getY(), sequencer.getRowHeight());

    // Advanced Mode: the sound panel takes the groove section's place.
    constexpr int soundW = 480, grooveW = 260, gap = 12;
    groove.setVisible (! advanced);
    sound.setBounds (bottom.removeFromLeft (advanced ? soundW + gap + grooveW : soundW));
    bottom.removeFromLeft (gap);
    if (! advanced)
    {
        groove.setBounds (bottom.removeFromLeft (grooveW));
        bottom.removeFromLeft (gap);
    }
    mixer.setBounds (bottom);
}

void MainComponent::timerCallback()
{
    // The mode can change underneath us when the host restores a project.
    const bool storedAdvanced = (bool) proc.apvts.state.getProperty (kAdvancedProperty, false);
    if (storedAdvanced != advanced)
        setAdvanced (storedAdvanced);

    header.refresh();
    instruments.refresh();
    sequencer.refresh();
    sound.refresh();
    mixer.refresh();

}

void MainComponent::patternEdited()
{
    sequencer.patternChanged();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    if (mods.isCommandDown())
    {
        if (code == 'Z' && ! mods.isShiftDown()) { if (proc.patterns.undo()) sequencer.patternChanged(); return true; }
        if (code == 'Y' || (code == 'Z' && mods.isShiftDown())) { if (proc.patterns.redo()) sequencer.patternChanged(); return true; }
        if (code == 'C') { sequencer.copyPattern(); return true; }
        if (code == 'V') { sequencer.pastePattern(); return true; }
        return false;
    }

    if (code == juce::KeyPress::spaceKey) { proc.setInternalPlaying (! proc.isInternalPlaying()); return true; }
    if (code == juce::KeyPress::leftKey)  { sequencer.moveSelection (-1, 0); return true; }
    if (code == juce::KeyPress::rightKey) { sequencer.moveSelection (1, 0); return true; }
    if (code == juce::KeyPress::upKey)    { sequencer.moveSelection (0, -1); return true; }
    if (code == juce::KeyPress::downKey)  { sequencer.moveSelection (0, 1); return true; }
    if (code == juce::KeyPress::returnKey) { sequencer.toggleSelectedStep(); return true; }

    // Computer-keyboard pads: A S D F G H J K trigger the eight voices.
    static constexpr char padKeys[kNumVoices] = { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K' };
    const auto ch = (char) juce::CharacterFunctions::toUpperCase (key.getTextCharacter());
    for (int v = 0; v < kNumVoices; ++v)
        if (ch == padKeys[v])
        {
            proc.triggerVoice (v, mods.isShiftDown() ? 1.0f : 0.8f);
            state.selectVoice (v);
            return true;
        }

    return false;
}

// ---------------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------------
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (proc.canLoadSample (f))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    // Drop onto an instrument row targets that voice, otherwise the selected one.
    int voice = instruments.rowAt (instruments.getLocalPoint (this, juce::Point<int> (x, y)));
    if (voice < 0)
        voice = state.voice;

    for (const auto& f : files)
        if (proc.canLoadSample (f))
        {
            loadSampleFile (voice, juce::File (f));
            break;
        }
}

void MainComponent::loadSampleFile (int voice, const juce::File& file)
{
    juce::String error;
    if (proc.loadSample (voice, file, error))
    {
        state.selectVoice (voice);
        proc.triggerVoice (voice, 0.85f);
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load Sample", error);
    }
}

void MainComponent::chooseSample (int voice)
{
    chooser = std::make_unique<juce::FileChooser> ("Load sample for " + juce::String (kVoiceSpecs[(size_t) voice].name),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   proc.getSampleWildcard());
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, voice] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->loadSampleFile (voice, fc.getResult());
                          });
}

// ---------------------------------------------------------------------------
// Pattern files
// ---------------------------------------------------------------------------
void MainComponent::exportPattern()
{
    const auto folder = PresetManager::getUserPatternFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Export pattern", folder.getChildFile ("pattern.fppattern"), "*.fppattern");
    const auto json = serial::patternToJson (proc.patterns.getPattern (sequencer.getEditPattern()));
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [json] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File() && ! f.withFileExtension ("fppattern").replaceWithText (json))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", "Could not write the file.");
                          });
}

void MainComponent::importPattern()
{
    chooser = std::make_unique<juce::FileChooser> ("Import pattern", PresetManager::getUserPatternFolder(), "*.fppattern;*.json");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || ! fc.getResult().existsAsFile())
                                  return;
                              if (auto p = serial::patternFromJson (fc.getResult().loadFileAsString()))
                              {
                                  safe->proc.patterns.setPattern (safe->sequencer.getEditPattern(), *p);
                                  safe->patternEdited();
                              }
                              else
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                                          "Not a valid Floorplan pattern file.");
                              }
                          });
}

void MainComponent::exportBank()
{
    const auto folder = PresetManager::getUserPatternFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Export pattern bank", folder.getChildFile ("bank.fpbank"), "*.fpbank");
    const auto json = serial::bankToJson (proc.patterns.getBank());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [json] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File() && ! f.withFileExtension ("fpbank").replaceWithText (json))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", "Could not write the file.");
                          });
}

void MainComponent::importBank()
{
    chooser = std::make_unique<juce::FileChooser> ("Import pattern bank", PresetManager::getUserPatternFolder(), "*.fpbank;*.json");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || ! fc.getResult().existsAsFile())
                                  return;
                              if (auto bank = serial::bankFromJson (fc.getResult().loadFileAsString()))
                              {
                                  safe->proc.patterns.setBank (*bank);
                                  safe->patternEdited();
                              }
                              else
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
                                                                          "Not a valid Floorplan bank file.");
                              }
                          });
}

// ---------------------------------------------------------------------------
void MainComponent::showSettingsMenu()
{
    juce::PopupMenu factoryPatterns;
    const auto& list = factory::patterns();
    for (int i = 0; i < (int) list.size(); ++i)
        factoryPatterns.addItem (100 + i, list[(size_t) i].name);

    juce::PopupMenu kitMenu;
    kitMenu.addItem (20, "Load kit file...");
    kitMenu.addItem (21, "Open user kit folder");
    kitMenu.addItem (22, "Rescan user kits");

    const bool midiOut = proc.apvts.getRawParameterValue (ids::midiOut)->load() > 0.5f;
    const bool seqOn = proc.apvts.getRawParameterValue (ids::sequencerOn)->load() > 0.5f;

    juce::PopupMenu menu;
    menu.addSectionHeader ("SEQUENCER");
    menu.addItem (12, "Internal sequencer on (off: only MIDI input and the pads play)", true, seqOn);
    menu.addSeparator();
    menu.addSectionHeader ("PATTERNS");
    menu.addSubMenu ("Load factory pattern into current slot", factoryPatterns);
    menu.addItem (1, "Import pattern...");
    menu.addItem (2, "Export pattern...");
    menu.addItem (3, "Import bank (16 patterns)...");
    menu.addItem (4, "Export bank...");
    menu.addSeparator();
    menu.addSectionHeader ("KITS");
    menu.addSubMenu ("Kit files", kitMenu);
    menu.addItem (23, "Open samples folder (missing samples are searched here)");
    menu.addSeparator();
    menu.addSectionHeader ("MIDI");
    menu.addItem (13, "MIDI note map...");
    menu.addItem (10, "Send sequencer notes to MIDI output", true, midiOut);
    menu.addItem (11, "Reset MIDI note map to defaults (GM)");
    menu.addSeparator();
    menu.addSectionHeader ("OUTPUTS");
    menu.addItem (14, "Each instrument to its own output (Out 2-9)");
    menu.addItem (15, "All instruments to the main output");
    menu.addSeparator();
    menu.addItem (30, "Keyboard shortcuts...");
    menu.addItem (31, "About Floorplan " FLOORPLAN_VERSION_STRING);

    juce::Component::SafePointer<MainComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;

        if (r >= 100)
        {
            self.proc.patterns.setPattern (self.sequencer.getEditPattern(), factory::patterns()[(size_t) (r - 100)].pattern);
            self.patternEdited();
            return;
        }

        switch (r)
        {
            case 1: self.importPattern(); break;
            case 2: self.exportPattern(); break;
            case 3: self.importBank(); break;
            case 4: self.exportBank(); break;
            case 10:
                if (auto* p = self.proc.apvts.getParameter (ids::midiOut))
                    p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                break;
            case 11: self.proc.resetVoiceNotes(); break;
            case 12:
                if (auto* p = self.proc.apvts.getParameter (ids::sequencerOn))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                    p->endChangeGesture();
                }
                break;
            case 13:
            {
                // Octave names differ between hosts: Ableton calls note 60 C3, FL Studio C5.
                juce::String text ("Note numbers, with their names in Ableton Live / FL Studio.\n"
                                   "Right-click an instrument to learn a different note.\n\n");
                for (int v = 0; v < kNumVoices; ++v)
                {
                    const int n = self.proc.getVoiceNote (v);
                    text << kVoiceSpecs[(size_t) v].name << ":   " << n << "   "
                         << juce::MidiMessage::getMidiNoteName (n, true, true, 3) << " / "
                         << juce::MidiMessage::getMidiNoteName (n, true, true, 5) << "\n";
                }
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MIDI note map", text);
                break;
            }
            case 14:
            case 15:
                for (int v = 0; v < kNumVoices; ++v)
                    if (auto* p = self.proc.apvts.getParameter (ids::output (v)))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 (r == 14 ? (float) (v + 1) : 0.0f));
                        p->endChangeGesture();
                    }
                break;
            case 23:
                FloorplanProcessor::getDefaultSampleFolder().createDirectory();
                FloorplanProcessor::getDefaultSampleFolder().startAsProcess();
                break;
            case 20:
            {
                self.chooser = std::make_unique<juce::FileChooser> ("Load kit", PresetManager::getUserFolder(), "*.fpkit");
                self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                           [safe] (const juce::FileChooser& fc)
                                           {
                                               if (safe != nullptr && fc.getResult().existsAsFile())
                                                   safe->proc.presets.loadFromFile (fc.getResult());
                                           });
                break;
            }
            case 21:
                PresetManager::getUserFolder().createDirectory();
                PresetManager::getUserFolder().startAsProcess();
                break;
            case 22:
                self.proc.presets.refreshUserList();
                self.header.updatePresetList();
                break;
            case 30:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Keyboard shortcuts",
                    "A S D F G H J K   trigger the eight voices (Shift = full velocity)\n"
                    "Space   start / stop the internal transport\n"
                    "Arrows   move the step selection, Enter toggles the step\n"
                    "Ctrl+Z / Ctrl+Y   undo / redo pattern edits\n"
                    "Ctrl+C / Ctrl+V   copy / paste the pattern\n\n"
                    "Grid: click / drag to paint, Shift+click accent, right-click step menu, wheel = velocity\n"
                    "Knobs: double-click resets, right-click for MIDI learn");
                break;
            case 31:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Floorplan",
                    "Floorplan " FLOORPLAN_VERSION_STRING "\nOpen-source House & EDM drum machine.\n"
                    "Licensed under the GNU GPL v3 or later. Built with JUCE (AGPLv3).");
                break;
            default: break;
        }
    });
}

} // namespace fp::ui
