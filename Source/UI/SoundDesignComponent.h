#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Context-sensitive synthesis controls for the selected voice, plus a
// waveform preview rendered offline (message thread) by the real voice DSP.
class SoundDesignComponent final : public juce::Component, private juce::ChangeListener
{
public:
    SoundDesignComponent (FloorplanProcessor& p, UiState& s);
    ~SoundDesignComponent() override;

    void refresh();                          // editor timer
    std::function<void (int)> onLoadSample;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void bindVoice();
    void renderPreview();
    float parameterHash() const;

    FloorplanProcessor& proc;
    UiState& state;
    juce::OwnedArray<Knob> synthKnobs;
    Knob filterKnob { proc }, driveKnob { proc }, levelKnob { proc }, panKnob { proc };
    Knob sampleTuneKnob { proc }, sampleDecayKnob { proc };
    juce::TextButton sampleButton { "LOAD SAMPLE" }, clearSampleButton { "X" };
    juce::TextButton auditionButton { "PLAY" };
    ParamCombo outputBox { proc };

    int boundVoice = -1;
    bool shownSample = false;
    float previewHash = -1.0f;
    juce::Rectangle<int> previewArea, infoArea;
    int shownNote = -1;
    std::vector<float> previewMin, previewMax;
    float previewSeconds = 1.0f;
};

} // namespace fp::ui
