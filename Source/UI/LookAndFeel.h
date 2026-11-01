#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fp::ui
{

// Palette: charcoal surfaces, one restrained amber accent.
namespace colours
{
inline const juce::Colour background  { 0xff111215 };
inline const juce::Colour panel       { 0xff191b1f };
inline const juce::Colour raised      { 0xff212429 };
inline const juce::Colour well        { 0xff0d0e10 };
inline const juce::Colour border      { 0xff2a2e35 };
inline const juce::Colour borderLight { 0xff3a3f48 };
inline const juce::Colour text        { 0xffe6e8ec };
inline const juce::Colour textDim     { 0xff8e949f };
inline const juce::Colour textFaint   { 0xff5b616c };
inline const juce::Colour accent      { 0xffff9f1c };
inline const juce::Colour accentHot   { 0xffffd27a };
inline const juce::Colour accentDim   { 0xff6b4512 };
inline const juce::Colour mute        { 0xffe5484d };
inline const juce::Colour solo        { 0xfff2c94c };
inline const juce::Colour meter       { 0xff3dd68c };
inline const juce::Colour meterHot    { 0xffff5f56 };
} // namespace colours

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// Draws a section panel with a small caps title; returns the content area.
juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);

class FloorplanLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FloorplanLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (13.0f, h * 0.55f), true); }
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};

} // namespace fp::ui
