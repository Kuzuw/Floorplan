#pragma once

#include "UI/Widgets.h"

namespace fp::ui
{

// Column geometry shared by the grid and the lane so they always line up.
struct GridGeometry
{
    int length = 16;   // pattern length
    int first = 0;     // first visible step
    int columns = 16;  // visible columns (<= 16)
    float x0 = 0.0f, colW = 1.0f;

    juce::Rectangle<float> column (int step, float top, float height) const
    {
        return { x0 + (float) (step - first) * colW, top, colW, height };
    }
    int stepAt (float x) const
    {
        const int c = (int) std::floor ((x - x0) / colW);
        return (c >= 0 && c < columns) ? first + c : -1;
    }
    bool inPattern (int step) const { return step >= 0 && step < length; }
};

class SequencerComponent final : public juce::Component, private juce::ChangeListener
{
public:
    SequencerComponent (FloorplanProcessor& p, UiState& s);
    ~SequencerComponent() override;

    void refresh();                         // editor timer (30 Hz)
    void patternChanged();                  // force reload from the store
    int getGridTop() const noexcept;        // y of the first grid row (local)
    int getRowHeight() const noexcept;

    // Pattern-level commands (also bound to keyboard shortcuts).
    void copyPattern();
    void pastePattern();
    void clearPattern();
    void doublePattern();
    void randomiseTrack();
    void showEuclidean();
    void rotateTrack (int amount);
    void moveSelection (int deltaStep, int deltaVoice);
    void toggleSelectedStep();
    int getEditPattern() const;

    void resized() override;

private:
    class Grid;
    class Lane;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateGeometry();
    void updateToolbar();
    void editPattern (const std::function<void (Pattern&)>& fn, bool pushUndo = true);
    void setEditPattern (int index);

    FloorplanProcessor& proc;
    UiState& state;
    Pattern cached;
    int cachedIndex = -1;
    uint32_t cachedVersion = 0xffffffffu;
    int playStep = -1, playPattern = -1;
    GridGeometry geo;

    juce::OwnedArray<juce::TextButton> patternButtons;
    juce::OwnedArray<juce::TextButton> pageButtons;
    juce::ComboBox lengthBox;
    juce::TextButton followButton { "FOLLOW" };
    juce::TextButton copyButton { "COPY" }, pasteButton { "PASTE" }, clearButton { "CLEAR" };
    juce::TextButton doubleButton { "x2" }, randomButton { "RAND" }, euclidButton { "EUCLID" };
    juce::OwnedArray<juce::TextButton> laneButtons;
    int laneMode = 0;

    std::unique_ptr<Grid> grid;
    std::unique_ptr<Lane> lane;

    JUCE_DECLARE_NON_COPYABLE (SequencerComponent)
};

} // namespace fp::ui
