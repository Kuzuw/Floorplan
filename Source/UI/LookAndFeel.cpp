#include "UI/LookAndFeel.h"

namespace fp::ui
{

namespace
{
bool isLight (juce::Colour c) { return c.getPerceivedBrightness() > 0.6f; }

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

// ---------------------------------------------------------------------------
// Surface primitives
// ---------------------------------------------------------------------------
void drawPlasticTexture (juce::Graphics& g, juce::Rectangle<int> area)
{
    // Fine, fixed-seed grain: a few percent of light and dark speckle reads as
    // moulded plastic rather than a flat fill.
    static const juce::Image tile = []
    {
        juce::Image img (juce::Image::ARGB, 128, 128, true);
        juce::Random rng (0x5eed);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const int n = rng.nextInt (100);
                if (n < 18)      img.setPixelAt (x, y, juce::Colours::black.withAlpha ((float) rng.nextInt (7) / 255.0f + 0.01f));
                else if (n < 36) img.setPixelAt (x, y, juce::Colours::white.withAlpha ((float) rng.nextInt (9) / 255.0f + 0.015f));
            }
        return img;
    }();

    juce::Graphics::ScopedSaveState save (g);
    g.setTiledImageFill (tile, 0, 0, 1.0f);
    g.fillRect (area);
}

juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle)
{
    auto header = area.removeFromTop (18);
    const auto titleFont = font (11.5f, true).withExtraKerningFactor (0.06f);
    const auto upper = title.toUpperCase();
    int x = header.getX();

    g.setColour (colours::text);
    g.setFont (titleFont);
    g.drawText (upper, header, juce::Justification::centredLeft);
    x += juce::GlyphArrangement::getStringWidthInt (titleFont, upper) + 8;

    if (subtitle.isNotEmpty())
    {
        const auto subFont = font (10.5f, true);
        const auto sub = subtitle.toUpperCase();
        g.setColour (colours::accentHot);
        g.setFont (subFont);
        g.drawText (sub, header.withLeft (x), juce::Justification::centredLeft);
        x += juce::GlyphArrangement::getStringWidthInt (subFont, sub) + 8;
    }

    // Printed rule running to the end of the section.
    if (x < header.getRight())
    {
        g.setColour (colours::borderLight.withAlpha (0.7f));
        g.fillRect ((float) x, (float) header.getCentreY(), (float) (header.getRight() - x), 1.0f);
    }

    area.removeFromTop (4);
    return area;
}

juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    // Inserted plate: slightly lighter than the body, with a moulded part line
    // (dark outer edge, light inner bevel).
    const auto r = area.toFloat();
    constexpr float corner = 7.0f;
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, corner);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        drawPlasticTexture (g, area);
    }
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, 1.0f);

    auto content = area.reduced (12, 9);
    if (title.isNotEmpty())
        content = drawSectionTitle (g, content, title);
    return content;
}

juce::Rectangle<float> drawLcd (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float bezel = juce::jlimit (2.0f, 4.0f, area.getHeight() * 0.08f);
    g.setColour (juce::Colours::white.withAlpha (0.5f)); // lip catching the light under the bezel
    g.fillRoundedRectangle (area.translated (0.0f, 1.0f), 4.0f);
    g.setColour (juce::Colour (0xff2c2b29));
    g.fillRoundedRectangle (area, 4.0f);

    auto glass = area.reduced (bezel);
    g.setColour (colours::lcd);
    g.fillRoundedRectangle (glass, 2.0f);

    // Reflection on the upper part of the glass, and the bezel's inner shadow.
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                juce::Colours::white.withAlpha (0.0f), glass.getX(), glass.getY() + glass.getHeight() * 0.5f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (glass.getX() + 1.0f, glass.getY(), glass.getWidth() - 2.0f, 1.0f);
    return glass.reduced (1.0f);
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
    // Segments lean right, pivoting on the baseline, like real segment glass.
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

        g.setColour (colours::lcdGhost);
        g.fillPath (ghost);
        g.setColour (lit);
        g.fillPath (on);
        x += w + gap;
    }
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float brightness)
{
    brightness = juce::jlimit (0.0f, 1.0f, brightness);
    const auto socket = juce::Rectangle<float> (radius * 2.0f + 2.0f, radius * 2.0f + 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillEllipse (socket);

    if (brightness > 0.0f)
    {
        g.setColour (colour.withAlpha (0.22f * brightness));
        g.fillEllipse (juce::Rectangle<float> (radius * 5.0f, radius * 5.0f).withCentre (centre));
    }

    const auto offColour = juce::Colour (0xff57534d);
    g.setColour (offColour.interpolatedWith (colour, brightness));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    g.setColour (juce::Colours::white.withAlpha (0.25f + 0.35f * brightness));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (centre.translated (-radius * 0.3f, -radius * 0.35f)));
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillEllipse (head.expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d6d0), head.getX(), head.getY(),
                                             juce::Colour (0xff9e9a93), head.getRight(), head.getBottom(), false));
    g.fillEllipse (head);
    // Cross-head recess.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    const float s = radius * 0.55f;
    g.drawLine (centre.x - s, centre.y, centre.x + s, centre.y, radius * 0.3f);
    g.drawLine (centre.x, centre.y - s, centre.x, centre.y + s, radius * 0.3f);
}

juce::Rectangle<float> keyFace (juce::Rectangle<float> bounds, bool pressed)
{
    const float travel = juce::jlimit (1.5f, 3.5f, bounds.getHeight() * 0.11f);
    auto face = bounds.withTrimmedBottom (travel);
    return pressed ? face.translated (0.0f, travel * 0.6f) : face;
}

juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour face, bool pressed, bool hover)
{
    const float corner = juce::jlimit (2.0f, 5.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.16f);
    const bool light = isLight (face);

    // Contact shadow on the body plastic.
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.08f : 0.13f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f).expanded (0.5f, 0.0f), corner + 1.0f);
    g.setColour (juce::Colours::black.withAlpha (pressed ? 0.1f : 0.16f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 0.7f), corner);

    // Side wall, visible below the top face.
    g.setColour (face.darker (light ? 0.32f : 0.55f));
    g.fillRoundedRectangle (bounds, corner);

    // Top face: soft vertical falloff, brighter under the cursor.
    const auto top = keyFace (bounds, pressed);
    const auto f = hover ? face.brighter (light ? 0.04f : 0.12f) : face;
    g.setGradientFill (juce::ColourGradient (f.brighter (light ? 0.03f : 0.08f), top.getX(), top.getY(),
                                             f.darker (light ? 0.05f : 0.08f), top.getX(), top.getBottom(), false));
    g.fillRoundedRectangle (top, corner);

    // Edge highlight where the face meets the top bevel.
    g.setColour (juce::Colours::white.withAlpha (light ? 0.7f : 0.16f));
    g.fillRect (top.getX() + corner * 0.7f, top.getY() + 0.6f, top.getWidth() - corner * 1.4f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (light ? 0.22f : 0.45f));
    g.drawRoundedRectangle (bounds.reduced (0.3f), corner, 0.8f);
    return top;
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
    setColour (juce::TextButton::buttonColourId, keyLight);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, keyText);
    setColour (juce::ComboBox::backgroundColourId, lcd);
    setColour (juce::ComboBox::outlineColourId, ink);
    setColour (juce::ComboBox::textColourId, lcdText);
    setColour (juce::ComboBox::arrowColourId, lcdDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, keyText);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, ink);
    setColour (juce::TooltipWindow::outlineColourId, borderLight);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, borderLight);
    setColour (juce::TextEditor::backgroundColourId, raised);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::outlineColourId, borderLight);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ToggleButton::textColourId, textDim);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
    setColour (juce::ScrollBar::thumbColourId, borderLight);
}

void FloorplanLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (1.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();

    // Printed scale: track, value arc and end ticks.
    const float ring = radius * 0.9f, ringWidth = juce::jmax (1.5f, radius * 0.07f);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::border);
    g.strokePath (track, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? colours::accent : colours::textFaint);
    g.strokePath (value, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    g.setColour (colours::textFaint);
    for (auto a : { startAngle, endAngle })
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius * 0.98f, a),
                                       centre.getPointOnCircumference (radius * 1.08f, a)), 1.0f);

    // Plastic cap: cast shadow, skirt, then the slightly domed top face.
    const auto cap = capColour (slider, colours::keyLight);
    const bool light = isLight (cap);
    const float cr = radius * 0.7f;
    auto circle = [] (juce::Point<float> c, float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };

    g.setColour (juce::Colours::black.withAlpha (0.1f));
    g.fillEllipse (circle (centre.translated (0.0f, cr * 0.2f), cr * 1.1f));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (circle (centre.translated (0.0f, cr * 0.1f), cr * 1.02f));

    g.setColour (cap.darker (light ? 0.3f : 0.5f));
    g.fillEllipse (circle (centre, cr));

    const auto topCentre = centre.translated (0.0f, -cr * 0.07f);
    const float topR = cr * 0.84f;
    juce::ColourGradient dome (cap.brighter (light ? 0.05f : 0.18f), topCentre.x - topR * 0.4f, topCentre.y - topR * 0.5f,
                               cap.darker (light ? 0.06f : 0.1f), topCentre.x + topR * 0.6f, topCentre.y + topR * 0.8f, true);
    g.setGradientFill (dome);
    g.fillEllipse (circle (topCentre, topR));
    g.setColour (juce::Colours::white.withAlpha (light ? 0.6f : 0.15f));
    g.drawEllipse (circle (topCentre, topR - 0.5f), 0.8f);
    g.setColour (juce::Colours::black.withAlpha (light ? 0.25f : 0.5f));
    g.drawEllipse (circle (centre, cr), 0.8f);

    // Pointer line moulded into the top face.
    const auto tip = topCentre.getPointOnCircumference (topR * 0.82f, angle);
    const auto inner = topCentre.getPointOnCircumference (topR * 0.32f, angle);
    g.setColour ((light ? colours::ink : colours::keyText).withMultipliedAlpha (enabled ? 1.0f : 0.4f));
    juce::Path pointer;
    pointer.startNewSubPath (inner);
    pointer.lineTo (tip);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.6f, radius * 0.075f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FloorplanLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool enabled = slider.isEnabled();

    if (style == juce::Slider::LinearBarVertical)
    {
        // Segment bar graph in a small display window.
        const auto glass = drawLcd (g, r);
        const bool bipolar = (bool) slider.getProperties()["bipolar"];
        const float norm = juce::jlimit (0.0f, 1.0f, (r.getBottom() - pos) / r.getHeight());

        const auto area = glass.reduced (4.0f, 4.0f);
        constexpr float segH = 3.0f, segGap = 2.0f;
        const int n = juce::jmax (4, (int) ((area.getHeight() + segGap) / (segH + segGap)));
        const float used = (float) n * (segH + segGap) - segGap;
        const float bottom = area.getBottom() - (area.getHeight() - used) * 0.5f;
        const int litTop = juce::roundToInt (norm * (float) n);
        const int centreSeg = n / 2;

        for (int i = 0; i < n; ++i)
        {
            const auto seg = juce::Rectangle<float> (area.getX(), bottom - (float) (i + 1) * (segH + segGap) + segGap, area.getWidth(), segH);
            bool on;
            if (bipolar) on = litTop >= centreSeg ? (i >= centreSeg && i < litTop) : (i >= litTop && i < centreSeg);
            else         on = i < litTop;

            const bool edge = on && (bipolar ? (litTop >= centreSeg ? i == litTop - 1 : i == litTop) : i == litTop - 1);
            g.setColour (! on ? colours::lcdGhost
                              : (! enabled ? colours::lcdDim : (edge ? colours::accent : colours::lcdText)));
            g.fillRect (seg);
        }
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        // Recessed slot with printed ticks and a plastic fader cap.
        const auto slot = juce::Rectangle<float> (6.0f, r.getHeight() - 8.0f).withCentre (r.getCentre());
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), 3.0f);
        g.setColour (juce::Colour (0xff2a2826));
        g.fillRoundedRectangle (slot, 3.0f);

        g.setColour (colours::textFaint.withAlpha (0.8f));
        for (int i = 0; i <= 8; ++i)
        {
            const float ty = slot.getY() + slot.getHeight() * (float) i / 8.0f;
            const float len = i % 4 == 0 ? 5.0f : 3.0f;
            g.fillRect (slot.getX() - 3.0f - len, ty - 0.5f, len, 1.0f);
        }

        const auto capBounds = juce::Rectangle<float> (juce::jmin (r.getWidth() - 2.0f, 22.0f), 13.0f)
                                   .withCentre ({ r.getCentreX(), juce::jlimit (slot.getY(), slot.getBottom(), pos) });
        const auto face = drawKey (g, capBounds, enabled ? colours::keyLight : colours::panel, false);
        g.setColour (colours::ink);
        g.fillRect (face.withSizeKeepingCentre (face.getWidth() - 6.0f, 1.2f));
        return;
    }

    // Horizontal: slot and a small cap.
    const auto slot = juce::Rectangle<float> (r.getWidth() - 8.0f, 5.0f).withCentre (r.getCentre());
    g.setColour (juce::Colour (0xff2a2826));
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (slot.withRight (juce::jmax (slot.getX(), pos)).reduced (0.0f, 1.5f), 1.0f);
    drawKey (g, juce::Rectangle<float> (10.0f, 16.0f).withCentre ({ pos, r.getCentreY() }), colours::keyLight, false);
}

juce::Colour FloorplanLookAndFeel::faceColour (juce::Button& b)
{
    juce::Colour face;
    if (b.getToggleState())
        face = b.findColour (juce::TextButton::buttonOnColourId);
    else
        switch (keyStyleOf (b))
        {
            case KeyStyle::dark:   face = colours::keyDark; break;
            case KeyStyle::orange: face = colours::accent; break;
            case KeyStyle::light:
            default:               face = colours::keyLight; break;
        }
    return b.isEnabled() ? face : face.interpolatedWith (colours::background, 0.45f);
}

void FloorplanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    // Latched toggles sit lower, like a key held in.
    drawKey (g, b.getLocalBounds().toFloat().reduced (0.5f, 0.0f), faceColour (b), down || b.getToggleState(), over && b.isEnabled());
}

void FloorplanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto face = keyFace (b.getLocalBounds().toFloat().reduced (0.5f, 0.0f), down || b.getToggleState());
    const auto faceCol = faceColour (b);
    const auto id = b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    auto colour = b.isColourSpecified (id) ? b.findColour (id) : (isLight (faceCol) ? colours::ink : colours::keyText);
    if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.45f);

    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (colour);
    g.drawFittedText (b.getButtonText(), face.toNearestInt().reduced (3, 1), juce::Justification::centred, 1, 0.75f);
}

void FloorplanLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // Values live in small display windows.
    const auto glass = drawLcd (g, juce::Rectangle<int> (0, 0, w, h).toFloat());
    if (w < 44)
        return; // compact boxes (e.g. chain slots) show only their value

    const float ax = glass.getRight() - 8.0f, ay = glass.getCentreY();
    juce::Path arrow;
    arrow.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.3f));
    g.fillPath (arrow);
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
    g.fillAll (colours::raised);
    g.setColour (colours::borderLight);
    g.drawRect (0, 0, w, h, 1);
}

void FloorplanLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colours::raised);
    g.setColour (colours::borderLight);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (colours::ink);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

void FloorplanLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (colours::panel);
    g.fillPath (path);
    g.setColour (colours::borderLight);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

} // namespace fp::ui
