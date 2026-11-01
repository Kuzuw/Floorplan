#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fp::ui
{

// Palette: a flat, warm off-white instrument body with near-black print and
// one signal orange. Orange always means "active / playing / being edited".
// Three family colours mark the instrument groups and nothing else.
namespace colours
{
inline const juce::Colour background  { 0xffefebe3 }; // body
inline const juce::Colour panel       { 0xfff5f2ec }; // section surfaces
inline const juce::Colour line        { 0xffd2cdc3 }; // thin dividers and section outlines
inline const juce::Colour hover       { 0xffe5e0d6 }; // hover and alternate beat bands
inline const juce::Colour ink         { 0xff1c1c1b }; // print, outlines, set steps, selection
inline const juce::Colour inkSoft     { 0xff3b3a38 };
inline const juce::Colour grey        { 0xff86827a }; // secondary text
inline const juce::Colour greyLight   { 0xffbfbab0 }; // inactive marks and tracks
inline const juce::Colour paper       { 0xfffbfaf7 }; // control faces, text on ink
inline const juce::Colour accent      { 0xffff5a1f }; // signal orange
inline const juce::Colour accentText  { 0xffd9430c }; // orange text on the light body
inline const juce::Colour danger      { 0xffe5484d }; // clipping
inline const juce::Colour screen      { 0xff151515 }; // display windows
inline const juce::Colour screenText  { 0xfff2efe8 };
inline const juce::Colour screenDim   { 0xff7b776f };
inline const juce::Colour screenGhost { 0x1cffffff }; // unlit segments and off values

// Instrument families.
inline const juce::Colour familyTonal { 0xfff3b63c }; // kick, percussion, tom
inline const juce::Colour familyNoise { 0xffe5484d }; // snare, clap
inline const juce::Colour familyMetal { 0xff86b6e3 }; // hats, cymbal
} // namespace colours

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

// Small uppercase label font used for parameter names and section titles.
inline juce::Font labelFont (float height = 10.5f)
{
    return font (height, true).withExtraKerningFactor (0.07f);
}

juce::Colour familyColour (int voice);

// --- Surface primitives ------------------------------------------------------

// Section label followed by a thin rule; returns the area below.
// The optional subtitle is printed in grey after the title.
juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle = {});

// A flat section surface with a thin outline. Returns the content area.
juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);

// Flat black display window; returns the usable inner area.
juce::Rectangle<float> drawScreen (juce::Graphics& g, juce::Rectangle<float> area);

// Seven-segment readout. Unlit segments stay faintly visible.
// Supports 0-9, '-', ' ' and '.' (drawn after the preceding digit).
void drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour lit, juce::Justification justification = juce::Justification::centredRight);

// Flat indicator dot: an outline when off, filled with the colour when on.
void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float brightness);

// Instrument badge: a family-coloured square with the voice's short name.
void drawBadge (juce::Graphics& g, juce::Rectangle<float> area, int voice);

// Button styles, stored as the component property "key".
enum class KeyStyle { light, dark, orange };
void setKeyStyle (juce::Component& c, KeyStyle style);

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
    juce::Font getComboBoxFont (juce::ComboBox&) override { return monoFont (12.5f, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (12.0f, (float) h * 0.5f), true); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;

private:
    static juce::Colour faceColour (juce::Button&, bool over, bool down);
};

} // namespace fp::ui
