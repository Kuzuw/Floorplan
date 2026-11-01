#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Column of eight instrument modules aligned with the grid rows: family badge
// (click to audition), name (click to select), activity dot, mute and solo.
// Right-click a module for samples, MIDI note learn and track tools.
class InstrumentComponent final : public juce::Component, private juce::ChangeListener
{
public:
    InstrumentComponent (FloorplanProcessor& p, UiState& s);
    ~InstrumentComponent() override;

    void refresh();                         // editor timer
    int rowAt (juce::Point<int> localPos) const;
    juce::Rectangle<int> rowBounds (int voice) const;
    void setRowHeight (int top, int height) { gridTop = top; rowHeight = height; resized(); }

    std::function<void()> onPatternEdited;  // track tools changed the pattern
    std::function<void (int)> onLoadSample; // open the sample chooser for a voice

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Row (InstrumentComponent& owner, int voice);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void refresh();
        juce::Rectangle<float> badgeArea() const;

        InstrumentComponent& owner;
        const int voice;
        ParamToggle muteButton, soloButton;
        float flash = 0.0f;
        uint32_t lastCount = 0;
        bool shownSelected = false, shownSample = false, shownMuted = false;
        int shownNote = -1;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showRowMenu (int voice, Row& row);

    FloorplanProcessor& proc;
    UiState& state;
    juce::OwnedArray<Row> rows;
    int gridTop = 0, rowHeight = 30;
};

} // namespace fp::ui
