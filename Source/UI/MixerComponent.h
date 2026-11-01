#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Eight channel strips (pan, fader, meter) + master (volume, clip, comp, meter).
// Mute and solo live on the instrument modules. A second page, CHARACTER,
// replaces the strips with the master bus Character section; the master
// column stays on both pages.
class MixerComponent final : public juce::Component, private juce::ChangeListener
{
public:
    MixerComponent (FloorplanProcessor& p, UiState& s);
    ~MixerComponent() override;

    void refresh();   // meters, editor timer

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Strip final : juce::Component
    {
        Strip (FloorplanProcessor& p, UiState& s, int voice);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override { state.selectVoice (voice); }

        FloorplanProcessor& proc;
        UiState& state;
        const int voice;
        juce::Slider fader;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment;
        Knob pan;
        LevelMeter meter;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); for (auto* s : strips) s->repaint(); }
    void showPage (bool character);
    bool characterOn() const;

    FloorplanProcessor& proc;
    UiState& state;
    juce::OwnedArray<Strip> strips;
    juce::Slider masterFader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    Knob clipKnob, compKnob;
    LevelMeter masterMeter { 2 };

    juce::TextButton mixerTab { "MIXER" }, characterTab { "CHARACTER" };
    ParamToggle characterToggle;
    Knob colorKnob, punchKnob, varyKnob, velToneKnob;
    juce::Rectangle<int> characterHelp;
    bool characterPage = false;
    bool shownCharacterOn = false;
};

} // namespace fp::ui
