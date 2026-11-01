#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Sound controls for the selected voice, plus a waveform preview rendered
// offline (message thread) by the real voice DSP.
//
// Standard Mode shows the six main synthesis controls with Filter and Drive.
// Advanced Mode is a wider inspector with tabs (Source, Pitch/Env, Tone,
// Output) that expose every control of the voice, including the engine's
// extra parameters. Switching never changes a parameter.
class SoundDesignComponent final : public juce::Component, private juce::ChangeListener
{
public:
    SoundDesignComponent (FloorplanProcessor& p, UiState& s);
    ~SoundDesignComponent() override;

    void refresh();                          // editor timer
    void setAdvanced (bool shouldBeAdvanced);
    std::function<void (int)> onLoadSample;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    enum Tab { TabSource, TabEnvelope, TabTone, TabOutput, NumTabs };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void bindVoice();
    void renderPreview();
    float parameterHash() const;
    std::array<float, kMaxVoiceParams> currentParams() const;
    bool envelopeTabAvailable() const;
    std::vector<int> pageParams (ParamPage page) const;
    std::vector<juce::Component*> visibleControls();
    void layoutControls (juce::Rectangle<int> area, const std::vector<juce::Component*>& controls);
    void paintEnvelope (juce::Graphics&);

    FloorplanProcessor& proc;
    UiState& state;
    juce::OwnedArray<Knob> synthKnobs;
    Knob filterKnob { proc }, driveKnob { proc }, levelKnob { proc }, panKnob { proc };
    Knob sampleTuneKnob { proc }, sampleDecayKnob { proc };
    juce::TextButton sampleButton { "LOAD SAMPLE" }, clearSampleButton { "X" };
    juce::TextButton auditionButton { "PLAY" };
    juce::OwnedArray<juce::TextButton> tabButtons;
    ParamCombo outputBox { proc }, chokeBox { proc };

    bool advanced = false;
    int tab = TabSource;
    int boundVoice = -1;
    bool shownSample = false;
    float previewHash = -1.0f;
    juce::Rectangle<int> previewArea, infoArea, envelopeArea;
    int dividerX = -1;                       // standard mode: between source and tone controls
    int shownNote = -1;
    std::vector<float> previewMin, previewMax;
    EnvelopeCurves envelopes;                // Pitch/Env page, updated with the preview
    float previewSeconds = 1.0f;
};

} // namespace fp::ui
