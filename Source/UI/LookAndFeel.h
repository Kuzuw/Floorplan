#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fp::ui
{

// Palette, from the design language: four tones (Primary orange, Secondary
// warm charcoal, Tertiary warm grey, Neutral cream) and their shades. Cards
// sit on a slightly darker body. Orange means "active / playing / being
// edited"; the deep orange marks primary actions and selections.
namespace colours
{
inline const juce::Colour background  { 0xffdedad2 }; // body, behind the cards
inline const juce::Colour panel       { 0xfff3f0ea }; // cards
inline const juce::Colour neutral     { 0xffebe7df }; // Neutral: insets, secondary buttons, tracks
inline const juce::Colour line        { 0xffd9d3c9 }; // thin dividers and empty tracks
inline const juce::Colour hover       { 0xffe4dfd6 }; // hover and alternate beat bands
inline const juce::Colour outline     { 0xffb9aca1 }; // outlined buttons and fields
inline const juce::Colour ink         { 0xff403d39 }; // Secondary: outlines, set steps, inverted buttons
inline const juce::Colour inkSoft     { 0xff5f5b55 };
inline const juce::Colour text        { 0xff1f1d1b }; // headlines and values
inline const juce::Colour label       { 0xff5b4339 }; // body and label text (warm brown)
inline const juce::Colour grey        { 0xff8a857c }; // Tertiary: captions, inactive icons
inline const juce::Colour greyLight   { 0xffc8c2b8 }; // inactive marks
inline const juce::Colour paper       { 0xfffaf8f4 }; // control faces, text on dark
inline const juce::Colour accent      { 0xffff5e00 }; // Primary orange
inline const juce::Colour accentDeep  { 0xffa33a00 }; // primary buttons, selection
inline const juce::Colour accentText  { 0xffc24600 }; // orange text on light surfaces
inline const juce::Colour accentSoft  { 0xffffb48a }; // orange tint (on dark screens)
inline const juce::Colour danger      { 0xffc8232b }; // clipping, destructive actions
inline const juce::Colour screen      { 0xff1b1916 }; // display windows and meters
inline const juce::Colour screenText  { 0xfff3efe8 };
inline const juce::Colour screenDim   { 0xff7b766e };
inline const juce::Colour screenGhost { 0x1cffffff }; // unlit segments and off values

// Instrument families, in palette tones.
inline const juce::Colour familyTonal { 0xffff5e00 }; // kick, percussion, tom
inline const juce::Colour familyNoise { 0xffa33a00 }; // snare, clap
inline const juce::Colour familyMetal { 0xff8a857c }; // hats, cymbal
} // namespace colours

// Typography: Space Grotesk for headlines, Geist for body text and labels
// (both embedded, SIL Open Font License).
enum class Face { body, bodyMedium, bodySemiBold, headline, headlineBold };
juce::Typeface::Ptr typeface (Face face);

inline juce::Font fontOf (Face face, float height)
{
    return juce::Font (juce::FontOptions (typeface (face)).withHeight (height));
}

// Body text: Geist Regular, or SemiBold when bold.
inline juce::Font font (float height, bool bold = false)
{
    return fontOf (bold ? Face::bodySemiBold : Face::body, height);
}

// Numbers and values: Geist Medium (SemiBold when bold).
inline juce::Font monoFont (float height, bool bold = false)
{
    return fontOf (bold ? Face::bodySemiBold : Face::bodyMedium, height);
}

// Headlines: Space Grotesk Medium, or Bold.
inline juce::Font headlineFont (float height, bool bold = false)
{
    return fontOf (bold ? Face::headlineBold : Face::headline, height);
}

// Small uppercase label font used for parameter names and captions.
inline juce::Font labelFont (float height = 10.5f)
{
    return fontOf (Face::bodySemiBold, height).withExtraKerningFactor (0.06f);
}

juce::Colour familyColour (int voice);

// --- Surface primitives ------------------------------------------------------

// Card title in the headline face, as given (not upper-cased); returns the
// area below. The optional subtitle is printed in grey after the title.
juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle = {});

// A flat, rounded card. Returns the content area.
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

// Button styles, stored as the component property "key". light is the
// Outlined button, dark the Inverted one, orange the bright Label action and
// primary the deep orange Primary button. A toggled-on button fills with its
// buttonOnColourId (Secondary charcoal by default).
enum class KeyStyle { light, dark, orange, primary };
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
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (12.0f, (float) h * 0.5f), true).withExtraKerningFactor (0.03f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    static juce::Colour faceColour (juce::Button&, bool over, bool down);
};

} // namespace fp::ui
