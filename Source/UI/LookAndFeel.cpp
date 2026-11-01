#include "UI/LookAndFeel.h"
#include "Parameters/ParameterSpecs.h"

#include <FloorplanFonts.h>

namespace fp::ui
{

namespace
{
constexpr float kOutline = 1.5f;      // stroke width of knob and fader outlines at design size
constexpr float kButtonCorner = 2.0f; // buttons and fields are nearly square
constexpr float kCardCorner = 12.0f;

// Dark text on bright orange (as on the design's Label button), light text on
// deep orange, charcoal and grey.
bool isLight (juce::Colour c) { return c.getPerceivedBrightness() > 0.5f; }

juce::Colour capColour (const juce::Component& c, juce::Colour fallback)
{
    const auto& v = c.getProperties()["cap"];
    return v.isVoid() ? fallback : juce::Colour ((juce::uint32) (juce::int64) v);
}

KeyStyle keyStyleOf (const juce::Component& c)
{
    const auto& v = c.getProperties()["key"];
    return v.isVoid() ? KeyStyle::light : (KeyStyle) (int) v;
}
} // namespace

juce::Typeface::Ptr typeface (Face face)
{
    // Loaded once, on first use (message thread).
    static const std::array<juce::Typeface::Ptr, 5> faces {
        juce::Typeface::createSystemTypefaceFor (FloorplanFonts::GeistRegular_ttf, FloorplanFonts::GeistRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor (FloorplanFonts::GeistMedium_ttf, FloorplanFonts::GeistMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (FloorplanFonts::GeistSemiBold_ttf, FloorplanFonts::GeistSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (FloorplanFonts::SpaceGroteskMedium_ttf, FloorplanFonts::SpaceGroteskMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (FloorplanFonts::SpaceGroteskBold_ttf, FloorplanFonts::SpaceGroteskBold_ttfSize),
    };
    return faces[(size_t) face];
}

juce::Colour familyColour (int voice)
{
    switch (kVoiceSpecs[(size_t) juce::jlimit (0, kNumVoices - 1, voice)].engine)
    {
        case EngineKind::Snare:
        case EngineKind::Clap:  return colours::familyNoise;
        case EngineKind::Metal: return colours::familyMetal;
        case EngineKind::Kick:
        case EngineKind::Tone:
        default:                return colours::familyTonal;
    }
}

// ---------------------------------------------------------------------------
// Surface primitives
// ---------------------------------------------------------------------------
juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle)
{
    // As on the design's cards: a quiet title, no rule.
    auto header = area.removeFromTop (18);
    const auto titleFont = headlineFont (15.0f);

    g.setColour (colours::text);
    g.setFont (titleFont);
    g.drawText (title, header, juce::Justification::centredLeft);

    if (subtitle.isNotEmpty())
    {
        const int x = header.getX() + juce::GlyphArrangement::getStringWidthInt (titleFont, title) + 10;
        g.setColour (colours::grey);
        g.setFont (font (12.5f));
        g.drawText (subtitle, header.withLeft (x), juce::Justification::centredLeft);
    }

    area.removeFromTop (6);
    return area;
}

juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (area.toFloat(), kCardCorner);

    auto content = area.reduced (12, 10);
    if (title.isNotEmpty())
        content = drawSectionTitle (g, content, title);
    return content;
}

juce::Rectangle<float> drawScreen (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colours::screen);
    g.fillRoundedRectangle (area, 3.0f);
    return area.reduced (3.0f);
}

void drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour lit, juce::Justification justification)
{
    static constexpr uint8_t masks[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

    const float h = area.getHeight();
    const float w = h * 0.5f, gap = h * 0.16f, t = h * 0.12f, dot = t * 1.7f;

    float total = 0.0f;
    for (auto ch : text)
        total += ch == '.' ? dot : w + gap;
    total -= gap;

    float x = area.getX();
    if (justification.testFlags (juce::Justification::right))                  x = area.getRight() - total;
    else if (justification.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;

    const float y = area.getY();
    // Segments lean right, pivoting on the baseline.
    const auto lean = juce::AffineTransform::shear (-0.08f, 0.0f).translated ((y + h) * 0.08f, 0.0f);

    auto segment = [&] (juce::Path& p, float sx, float sy, float sw, float sh)
    {
        juce::Path s;
        s.addRoundedRectangle (sx, sy, sw, sh, t * 0.35f);
        p.addPath (s, lean);
    };

    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text[i];
        if (ch == '.')
        {
            g.setColour (lit);
            g.fillRect (x + (dot - t) * 0.5f - gap * 0.6f, y + h - t, t, t);
            x += dot;
            continue;
        }

        uint8_t mask = 0;
        if (ch >= '0' && ch <= '9') mask = masks[ch - '0'];
        else if (ch == '-')         mask = 0x40;

        const float half = h * 0.5f, inset = t * 0.55f;
        const juce::Rectangle<float> segs[7] = {
            { x + inset, y, w - 2 * inset, t },                                   // a
            { x + w - t, y + inset, t, half - inset - t * 0.15f },                // b
            { x + w - t, y + half + t * 0.15f, t, half - inset - t * 0.15f },     // c
            { x + inset, y + h - t, w - 2 * inset, t },                           // d
            { x, y + half + t * 0.15f, t, half - inset - t * 0.15f },             // e
            { x, y + inset, t, half - inset - t * 0.15f },                        // f
            { x + inset, y + half - t * 0.5f, w - 2 * inset, t },                 // g
        };

        juce::Path ghost, on;
        for (int s = 0; s < 7; ++s)
            segment ((mask >> s) & 1 ? on : ghost, segs[s].getX(), segs[s].getY(), segs[s].getWidth(), segs[s].getHeight());

        g.setColour (colours::screenGhost);
        g.fillPath (ghost);
        g.setColour (lit);
        g.fillPath (on);
        x += w + gap;
    }
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float brightness)
{
    brightness = juce::jlimit (0.0f, 1.0f, brightness);
    const auto dot = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    if (brightness > 0.0f)
    {
        g.setColour (colour.withAlpha (0.25f + 0.75f * brightness));
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (colours::greyLight);
        g.drawEllipse (dot.reduced (0.5f), 1.0f);
    }
}

void drawBadge (juce::Graphics& g, juce::Rectangle<float> area, int voice)
{
    const auto fill = familyColour (voice);
    g.setColour (fill);
    g.fillRoundedRectangle (area, kButtonCorner);
    g.setColour (isLight (fill) ? colours::text : colours::paper);
    g.setFont (font (juce::jmin (12.0f, area.getHeight() * 0.48f), true));
    g.drawText (kVoiceSpecs[(size_t) voice].shortName, area, juce::Justification::centred);
}

void setKeyStyle (juce::Component& c, KeyStyle style)
{
    c.getProperties().set ("key", (int) style);
    c.repaint();
}

// ---------------------------------------------------------------------------
// LookAndFeel
// ---------------------------------------------------------------------------
FloorplanLookAndFeel::FloorplanLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, ink);
    setColour (juce::ComboBox::backgroundColourId, paper);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, label);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDeep);
    setColour (juce::PopupMenu::highlightedTextColourId, paper);
    setColour (juce::PopupMenu::headerTextColourId, grey);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, paper);
    setColour (juce::TooltipWindow::outlineColourId, ink);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, line);
    setColour (juce::TextEditor::backgroundColourId, paper);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accentDeep);
    setColour (juce::ToggleButton::tickDisabledColourId, greyLight);
    setColour (juce::ScrollBar::thumbColourId, greyLight);
}

juce::Typeface::Ptr FloorplanLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Any text drawn with the default font (popup menus, alerts, editors)
    // uses Geist too.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return typeface (f.isBold() ? Face::bodySemiBold : Face::body);
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void FloorplanLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    // Flat encoder: thin value arc around a round cap with a pointer line.
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();
    const bool active = slider.isMouseButtonDown();

    const float arcR = radius - 1.5f, arcW = juce::jmax (2.0f, radius * 0.1f);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (enabled ? (active ? colours::accent : colours::ink) : colours::greyLight);
        g.strokePath (value, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto cap = capColour (slider, colours::paper);
    const float capR = radius * 0.7f;
    const auto capArea = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    g.setColour (enabled ? cap : colours::panel);
    g.fillEllipse (capArea);
    g.setColour (enabled ? colours::ink : colours::greyLight);
    g.drawEllipse (capArea, kOutline);

    juce::Path pointer;
    pointer.startNewSubPath (centre.getPointOnCircumference (capR * 0.2f, angle));
    pointer.lineTo (centre.getPointOnCircumference (capR - 1.0f, angle));
    const auto pointerColour = isLight (cap) ? colours::ink : colours::paper;
    g.setColour (enabled ? pointerColour : colours::greyLight);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.0f, radius * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FloorplanLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool enabled = slider.isEnabled();
    const auto ink = enabled ? colours::ink : colours::greyLight;

    if (style == juce::Slider::LinearVertical)
    {
        // Fader: a thin track filled from the bottom, and a solid flat cap.
        const auto track = juce::Rectangle<float> (3.0f, r.getHeight() - 10.0f).withCentre (r.getCentre());
        const float capY = juce::jlimit (track.getY(), track.getBottom(), pos);
        g.setColour (colours::line);
        g.fillRoundedRectangle (track, 1.5f);
        g.setColour (ink);
        g.fillRoundedRectangle (track.withTop (capY), 1.5f);

        g.setColour (colours::greyLight);
        for (int i = 0; i <= 4; ++i)
        {
            const float ty = track.getY() + track.getHeight() * (float) i / 4.0f;
            g.fillRect (track.getX() - 7.0f, ty - 0.5f, 4.0f, 1.0f);
        }

        const auto cap = juce::Rectangle<float> (juce::jmin (r.getWidth() - 2.0f, 22.0f), 11.0f).withCentre ({ r.getCentreX(), capY });
        g.setColour (slider.isMouseButtonDown() ? colours::accent : ink);
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (colours::paper);
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 8.0f, 1.5f));
        return;
    }

    // Horizontal: track with an orange fill and a small cap.
    const auto track = juce::Rectangle<float> (r.getWidth() - 10.0f, 3.0f).withCentre (r.getCentre());
    g.setColour (colours::line);
    g.fillRoundedRectangle (track, 1.5f);
    g.setColour (enabled ? colours::accentDeep : colours::greyLight);
    g.fillRoundedRectangle (track.withRight (juce::jmax (track.getX(), pos)), 1.5f);
    g.setColour (ink);
    g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, juce::jmin (r.getHeight(), 16.0f)).withCentre ({ pos, r.getCentreY() }), 2.0f);
}

juce::Colour FloorplanLookAndFeel::faceColour (juce::Button& b, bool over, bool down)
{
    juce::Colour face;
    if (b.getToggleState())
        face = b.findColour (juce::TextButton::buttonOnColourId);
    else
        switch (keyStyleOf (b))
        {
            case KeyStyle::dark:    face = over ? colours::inkSoft : colours::ink; break;
            case KeyStyle::orange:  face = over ? colours::accent.brighter (0.15f) : colours::accent; break;
            case KeyStyle::light:
            default:                face = down ? colours::line : (over ? colours::neutral : colours::panel); break;
        }
    if (down && b.getToggleState())
        face = face.interpolatedWith (colours::greyLight, 0.3f);
    return b.isEnabled() ? face : colours::neutral;
}

void FloorplanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    // Filled styles are flat; only the Outlined (light, off) style draws a frame.
    const auto r = b.getLocalBounds().toFloat();
    g.setColour (faceColour (b, over && b.isEnabled(), down));
    g.fillRoundedRectangle (r, kButtonCorner);
    if (keyStyleOf (b) == KeyStyle::light && ! b.getToggleState())
    {
        g.setColour (b.isEnabled() ? colours::outline : colours::line);
        g.drawRoundedRectangle (r.reduced (0.75f), kButtonCorner, 1.5f);
    }
}

void FloorplanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto face = faceColour (b, over, down);
    const auto id = b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    auto colour = b.isColourSpecified (id) ? b.findColour (id) : (isLight (face) ? colours::text : colours::paper);
    if (! b.isEnabled())
        colour = colours::greyLight;

    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (colour);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.75f);
}

void FloorplanLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // A field like the design's search box: neutral fill, thin warm outline.
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const bool enabled = box.isEnabled();
    const bool over = box.isMouseOver (true) && enabled;
    g.setColour (over ? colours::hover : colours::neutral);
    g.fillRoundedRectangle (r, kButtonCorner);
    g.setColour (enabled ? (over ? colours::accentText.withAlpha (0.6f) : colours::outline) : colours::line);
    g.drawRoundedRectangle (r, kButtonCorner, 1.0f);
    if (w < 44)
        return; // compact boxes (e.g. chain slots) show only their value

    // Chevron.
    const float ax = r.getRight() - 10.0f, ay = r.getCentreY();
    juce::Path arrow;
    arrow.startNewSubPath (ax - 3.5f, ay - 1.75f);
    arrow.lineTo (ax, ay + 1.75f);
    arrow.lineTo (ax + 3.5f, ay - 1.75f);
    g.setColour (enabled ? colours::label : colours::greyLight);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FloorplanLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const bool compact = box.getWidth() < 44;
    label.setBounds (compact ? juce::Rectangle<int> (2, 0, box.getWidth() - 4, box.getHeight())
                             : juce::Rectangle<int> (6, 0, box.getWidth() - 22, box.getHeight()));
    label.setJustificationType (compact ? juce::Justification::centred : juce::Justification::centredLeft);
    label.setFont (getComboBoxFont (box));
}

void FloorplanLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::panel);
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h, 1);
}

void FloorplanLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    // Inverted: charcoal with light text.
    g.fillAll (colours::ink);
    g.setColour (colours::paper);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

void FloorplanLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (colours::panel);
    g.fillPath (path);
    g.setColour (colours::line);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

} // namespace fp::ui
