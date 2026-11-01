#pragma once

#include "UI/GrooveComponent.h"
#include "UI/HeaderComponent.h"
#include "UI/InstrumentComponent.h"
#include "UI/MixerComponent.h"
#include "UI/SequencerComponent.h"
#include "UI/SoundDesignComponent.h"

namespace fp::ui
{

// The whole interface, laid out at a fixed design size and scaled by the
// editor. A single 30 Hz timer polls the processor's atomics; nothing here
// ever runs on, or waits for, the audio thread.
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    static constexpr int kDesignWidth = 1280;
    static constexpr int kDesignHeight = 800;

    explicit MainComponent (FloorplanProcessor& p);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void selectVoice (int voice) { state.selectVoice (voice); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void setAdvanced (bool shouldBeAdvanced);
    void showSettingsMenu();
    void chooseSample (int voice);
    void loadSampleFile (int voice, const juce::File& file);
    void exportPattern();
    void importPattern();
    void exportBank();
    void importBank();
    void patternEdited();

    FloorplanProcessor& proc;
    FloorplanLookAndFeel lookAndFeel;
    UiState state;
    HeaderComponent header { proc };
    InstrumentComponent instruments { proc, state };
    SequencerComponent sequencer { proc, state };
    SoundDesignComponent sound { proc, state };
    GrooveComponent groove { proc };
    MixerComponent mixer { proc, state };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Rectangle<int> sequencerPanel;
    bool advanced = false;
};

} // namespace fp::ui
