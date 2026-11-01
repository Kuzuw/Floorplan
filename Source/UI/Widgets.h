#pragma once

#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"

#include <optional>

namespace fp::ui
{

// Editor-wide selection and clipboard. Message thread only.
struct UiState : juce::ChangeBroadcaster
{
    int voice = 0;          // selected instrument
    int step = 0;           // selected step (absolute index in the pattern)
    int page = 0;           // visible 16-step page
    bool follow = true;     // page follows the playhead
    std::optional<Pattern> patternClipboard;
    std::optional<Track> trackClipboard;

    void selectVoice (int v) { if (v != voice) { voice = v; sendSynchronousChangeMessage(); } }
    void selectStep (int s)  { if (s != step)  { step = s;  sendSynchronousChangeMessage(); } }
};

// Common right-click menu for any parameter control.
void showParameterMenu (FloorplanProcessor& proc, const juce::String& paramId, juce::Component& target);

// Rotary encoder bound to a parameter. The full layout prints the name above
// and the value below (orange while dragging); the compact layout shows the
// name underneath and swaps in the value while hovering or dragging.
// Double-click resets, right-click opens the parameter menu (MIDI learn).
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Layout { full, compact };

    Knob (FloorplanProcessor& p, const juce::String& paramId = {}, const juce::String& label = {}, bool bipolar = false);
    ~Knob() override;

    void bind (const juce::String& paramId, const juce::String& label, bool bipolar = false);
    void unbind();
    void setLayout (Layout newLayout);
    void setCap (juce::Colour capColour); // cap fill colour
    const juce::String& getParamId() const noexcept { return paramId; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

private:
    struct KnobSlider final : juce::Slider
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
            juce::Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
    };

    FloorplanProcessor& proc;
    KnobSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String paramId, label;
    Layout layout = Layout::full;
    bool hovering = false, dragging = false;
};

// Toggle button bound to a bool parameter.
class ParamToggle final : public juce::TextButton
{
public:
    ParamToggle (FloorplanProcessor& p, const juce::String& text, juce::Colour onColour);
    void bind (const juce::String& paramId);

private:
    FloorplanProcessor& proc;
    juce::String paramId;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    void mouseDown (const juce::MouseEvent& e) override;
};

// Combo box bound to a choice parameter.
class ParamCombo final : public juce::ComboBox
{
public:
    explicit ParamCombo (FloorplanProcessor& p, const juce::String& paramId = {});
    void bind (const juce::String& paramId);

private:
    FloorplanProcessor& proc;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Lightweight peak meter with a decaying hold; repaints only when it changes.
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (int channels = 1) : numChannels (channels) {}
    void setLevels (float left, float right = 0.0f);
    void paint (juce::Graphics&) override;

private:
    int numChannels;
    float levels[2] {}, shown[2] {};
};

} // namespace fp::ui
