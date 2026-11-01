#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fp::ui
{

// Palette: near-black hardware panels with bone-white as the "on" colour,
// thin outlines, and amber reserved for accents and the playhead.
namespace colours
{
inline const juce::Colour background  { 0xff0e0e0d };
inline const juce::Colour panel       { 0xff141413 };
inline const juce::Colour raised      { 0xff1d1d1b };
inline const juce::Colour well        { 0xff090908 };
inline const juce::Colour border      { 0xff32312d };
inline const juce::Colour borderLight { 0xff5a5852 };
inline const juce::Colour text        { 0xfff0eadb };
inline const juce::Colour textDim     { 0xffa39d8f };
inline const juce::Colour textFaint   { 0xff5f5b53 };
inline const juce::Colour accent      { 0xffebe4d2 }; // bone: active steps, values, toggles
inline const juce::Colour accentHot   { 0xffffa62b }; // amber: accents, playhead, live values
inline const juce::Colour accentDim   { 0xff4a473f };
inline const juce::Colour ink         { 0xff121211 }; // text and marks drawn on bone
inline const juce::Colour mute        { 0xffe5484d };
inline const juce::Colour solo        { 0xfff2c94c };
inline const juce::Colour meter       { 0xffd9d2bf };
inline const juce::Colour meterHot    { 0xffff5f56 };
} // namespace colours

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

// Bold uppercase section title with a dashed rule underneath; returns the
// area below the rule.
juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle = {});

// Draws a section panel (optionally titled); returns the content area.
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
