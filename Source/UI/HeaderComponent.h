#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Top bar: logo, kit presets, undo/redo, display (position, clock, tempo),
// transport, swing, CPU, Advanced Mode and settings.
class HeaderComponent final : public juce::Component
{
public:
    explicit HeaderComponent (FloorplanProcessor& p);

    void refresh();                    // called from the editor timer (30 Hz)
    void updatePresetList();
    void setAdvanced (bool advanced);  // reflect the mode without notifying

    std::function<void()> onMenu;            // settings menu, handled by the main component
    std::function<void()> onUndoRedo;        // pattern view needs a refresh
    std::function<void (bool)> onAdvanced;   // Advanced Mode toggled by the user

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Large BPM readout; drag vertically to change the internal tempo.
    class BpmDisplay final : public juce::Component
    {
    public:
        explicit BpmDisplay (FloorplanProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void refresh();

    private:
        FloorplanProcessor& proc;
        double shownBpm = 0.0;
        bool shownHost = false;
        float dragStartValue = 0.0f;
    };

    void savePreset();

    FloorplanProcessor& proc;
    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::TextButton playButton { "PLAY" }, menuButton { "SETTINGS" }, advancedButton { "ADVANCED" };
    BpmDisplay bpm { proc };
    Knob swingKnob;
    juce::Rectangle<int> logoArea, lcdArea, cpuArea;
    juce::String status;
    bool statusIsLearn = false;
    int shownCpu = -1;
    bool shownPlaying = false;
};

} // namespace fp::ui
