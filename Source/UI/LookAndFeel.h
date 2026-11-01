#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fp::ui
{

// Palette: a moulded light-grey plastic body with printed (silkscreen) labels,
// smoked-glass LCD windows, and one signal orange for active states.
namespace colours
{
inline const juce::Colour background  { 0xffd6d3cc }; // body plastic
inline const juce::Colour panel       { 0xffe2dfd9 }; // inserted plates
inline const juce::Colour raised      { 0xfff0eee9 }; // light key face, menus
inline const juce::Colour well        { 0xffb3afa7 }; // recesses and slots
inline const juce::Colour border      { 0xffaeaaa2 }; // part lines
inline const juce::Colour borderLight { 0xff8a867e }; // stronger printed lines
inline const juce::Colour text        { 0xff262523 }; // silkscreen
inline const juce::Colour textDim     { 0xff5a5752 };
inline const juce::Colour textFaint   { 0xff8d8982 };
inline const juce::Colour accent      { 0xffff5a1f }; // signal orange
inline const juce::Colour accentHot   { 0xffe2470f }; // orange on light plastic (text)
inline const juce::Colour accentDim   { 0xffffb08f };
inline const juce::Colour ink         { 0xff1e1d1c }; // text on light keys
inline const juce::Colour keyDark     { 0xff302f2d };
inline const juce::Colour keyLight    { 0xffeeece6 };
inline const juce::Colour keyText     { 0xfff4f2ec }; // text on dark and orange keys
inline const juce::Colour lcd         { 0xff151617 };
inline const juce::Colour lcdText     { 0xfff1eee4 };
inline const juce::Colour lcdDim      { 0xff8f8c84 };
inline const juce::Colour lcdGhost    { 0x16ffffff }; // unlit segments
inline const juce::Colour lcdBlue     { 0xff47a6ff };
inline const juce::Colour mute        { 0xffe8432c };
inline const juce::Colour solo        { 0xfff2b330 };
inline const juce::Colour meter       { 0xfff1eee4 };
inline const juce::Colour meterHot    { 0xffff4a3d };
} // namespace colours

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

// --- Surface primitives ------------------------------------------------------

// Subtle moulded-plastic grain over an area (cached noise tile).
void drawPlasticTexture (juce::Graphics& g, juce::Rectangle<int> area);

// Printed section label followed by a thin rule; returns the area below.
// The optional subtitle is printed in orange, like a model designation.
juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle = {});

// A module plate set into the chassis: sunken groove edge (dark top/left,
// light bottom/right) and a screw in each corner. Returns the content area.
juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);

// Smoked-glass display window in a dark bezel; returns the glass area.
juce::Rectangle<float> drawLcd (juce::Graphics& g, juce::Rectangle<float> area);

// Seven-segment readout. Unlit segments stay faintly visible, like a real LCD.
// Supports 0-9, '-', ' ' and '.' (drawn after the preceding digit).
void drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour lit, juce::Justification justification = juce::Justification::centredRight);

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float brightness);
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius);

// Key travel: resting, latched (held in by a toggle) or pressed by the mouse.
enum class KeyState { up, latched, down };

// An extruded keycap in a dark drop well: lower skirt, top face with a
// vertical falloff and a highlight on the top chamfer. A pressed key's face
// travels down 3 px over its skirt. Returns the top face to print on.
juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour face,
                                KeyState state, bool hover = false);
juce::Rectangle<float> keyFace (juce::Rectangle<float> bounds, KeyState state);

// Chunky rectangular fader cap with a raised grip ridge and a cast shadow.
void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, bool enabled);

// Button properties: "key" = "light" (default), "dark" or "orange".
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
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (12.0f, h * 0.5f), true); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;

private:
    static juce::Colour faceColour (juce::Button&);
    static KeyState keyState (juce::Button&, bool down);
};

} // namespace fp::ui
