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
    // Module plate: a separate moulding set into the chassis. Its edge is a
    // sunken groove lit from the top left, so the top and left walls fall in
    // shadow and the bottom and right walls catch the light.
    const auto r = area.toFloat();
    constexpr float corner = 5.0f;
    juce::Path outline;
    outline.addRoundedRectangle (r, corner);

    g.setColour (colours::panel);
    g.fillPath (outline);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (outline);
        drawPlasticTexture (g, area);
    }

    // The two sides meet on 45-degree mitres at the top-right and bottom-left corners.
    const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    juce::Path shadowSide, lightSide;
    shadowSide.addQuadrilateral (r.getX(), r.getY(), r.getRight(), r.getY(), r.getRight() - d, r.getY() + d, r.getX() + d, r.getBottom() - d);
    shadowSide.addTriangle (r.getX(), r.getY(), r.getX() + d, r.getBottom() - d, r.getX(), r.getBottom());
    lightSide.addQuadrilateral (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX() + d, r.getBottom() - d, r.getRight() - d, r.getY() + d);
    lightSide.addTriangle (r.getRight(), r.getBottom(), r.getX(), r.getBottom(), r.getX() + d, r.getBottom() - d);

    auto strokeEdge = [&] (const juce::Path& side, juce::Colour colour)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (side);
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    };
    strokeEdge (shadowSide, juce::Colours::black.withAlpha (0.4f));
    strokeEdge (lightSide, juce::Colours::white.withAlpha (0.85f));

    // Fasteners holding the plate to the chassis.
    const auto screws = r.reduced (7.0f);
    for (auto p : { screws.getTopLeft(), screws.getTopRight(), screws.getBottomLeft(), screws.getBottomRight() })
        drawScrew (g, p, 3.4f);

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

namespace
{
// Keycap geometry, shared by drawing and by text placement on the face.
struct KeyGeometry
{
    juce::Rectangle<float> well, body, face;
    float corner, skirt;
};

KeyGeometry keyGeometry (juce::Rectangle<float> bounds, KeyState state)
{
    KeyGeometry k;
    k.well = bounds.withTrimmedBottom (1.0f); // 1 px left for the lip below the well
    k.body = k.well.reduced (1.5f);
    k.corner = juce::jlimit (1.5f, 4.0f, juce::jmin (k.body.getWidth(), k.body.getHeight()) * 0.14f);
    k.skirt = k.body.getHeight() >= 24.0f ? 4.0f : 3.0f;
    const float travel = state == KeyState::down ? 3.0f : (state == KeyState::latched ? 1.5f : 0.0f);
    k.face = k.body.withTrimmedBottom (k.skirt).translated (0.0f, travel);
    return k;
}
} // namespace

juce::Rectangle<float> keyFace (juce::Rectangle<float> bounds, KeyState state)
{
    return keyGeometry (bounds, state).face;
}

juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour face, KeyState state, bool hover)
{
    const auto k = keyGeometry (bounds, state);
    const bool light = isLight (face);

    // Drop well: the opening in the chassis, its lower lip catching the light.
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillRoundedRectangle (k.well.translated (0.0f, 1.0f), k.corner + 1.5f);
    g.setColour (juce::Colour (0xff161514));
    g.fillRoundedRectangle (k.well, k.corner + 1.5f);

    // Lower skirt: the front wall of the cap, darkening towards its base.
    // Dark caps keep a skirt a little lighter than the well so it still reads.
    const auto skirt = light ? face.darker (0.8f) : face.darker (0.1f);
    g.setGradientFill (juce::ColourGradient (skirt, 0.0f, k.body.getBottom() - k.skirt,
                                             skirt.darker (0.5f), 0.0f, k.body.getBottom(), false));
    g.fillRoundedRectangle (k.body, k.corner);

    // Top face: a subtle vertical falloff, a touch brighter under the cursor.
    const auto f = hover ? face.brighter (light ? 0.03f : 0.1f) : face;
    g.setGradientFill (juce::ColourGradient (f.brighter (light ? 0.04f : 0.1f), 0.0f, k.face.getY(),
                                             f.darker (light ? 0.07f : 0.12f), 0.0f, k.face.getBottom(), false));
    g.fillRoundedRectangle (k.face, k.corner);

    // Where the face overhangs the skirt it throws a thin shadow line.
    if (k.face.getBottom() < k.body.getBottom() - 0.5f)
    {
        g.setColour (juce::Colours::black.withAlpha (light ? 0.25f : 0.6f));
        g.fillRect (k.face.getX() + k.corner * 0.5f, k.face.getBottom(), k.face.getWidth() - k.corner, 1.0f);
    }

    // Highlight along the top inner chamfer.
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (k.face.getX() + k.corner * 0.6f, k.face.getY() + 1.0f, k.face.getWidth() - k.corner * 1.2f, 1.0f);
    return k.face;
}

void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool vertical, bool enabled)
{
    const auto face = enabled ? colours::keyDark : colours::keyDark.interpolatedWith (colours::background, 0.45f);
    constexpr float corner = 2.5f, skirt = 3.0f;

    // Cast shadow on the chassis, falling below the cap.
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillRoundedRectangle (cap.translated (0.0f, 4.0f).expanded (1.5f, 0.5f), corner + 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), corner);

    // Body: dark lower skirt with the top face above it.
    g.setColour (face.darker (0.7f));
    g.fillRoundedRectangle (cap, corner);
    const auto top = cap.withTrimmedBottom (skirt);
    g.setGradientFill (juce::ColourGradient (face.brighter (0.18f), 0.0f, top.getY(), face.darker (0.1f), 0.0f, top.getBottom(), false));
    g.fillRoundedRectangle (top, corner);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (top.getX() + corner, top.getY() + 1.0f, top.getWidth() - corner * 2.0f, 1.0f);

    // Raised grip ridge across the middle: lit edge, white index line, shadowed edge.
    const auto index = enabled ? colours::keyText : colours::keyText.withAlpha (0.4f);
    if (vertical)
    {
        auto ridge = top.withSizeKeepingCentre (top.getWidth() - 4.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (ridge.removeFromTop (1.0f));
        g.setColour (index);
        g.fillRect (ridge.removeFromTop (1.2f));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (ridge);
    }
    else
    {
        auto ridge = top.withSizeKeepingCentre (3.0f, top.getHeight() - 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRect (ridge.removeFromLeft (1.0f));
        g.setColour (index);
        g.fillRect (ridge.removeFromLeft (1.2f));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (ridge);
    }
}

namespace
{
// Black fader groove recessed into the chassis: shadowed top and left inner
// walls, a lit lower lip outside.
void drawGroove (juce::Graphics& g, juce::Rectangle<float> slot)
{
    const float corner = juce::jmin (slot.getWidth(), slot.getHeight()) * 0.5f;
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), corner);
    g.setColour (juce::Colour (0xff121110));
    g.fillRoundedRectangle (slot, corner);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (slot, corner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, slot.getY(),
                                             juce::Colours::transparentBlack, 0.0f, slot.getY() + 6.0f, false));
    g.fillRect (slot.withHeight (6.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (slot.withWidth (1.0f));
    g.fillRect (slot.withHeight (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRect (slot.withTrimmedLeft (slot.getWidth() - 1.0f));
    g.fillRect (slot.withTrimmedTop (slot.getHeight() - 1.0f));
}
} // namespace

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
    const auto centre = bounds.getCentre().translated (0.0f, -radius * 0.04f);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();
    auto circle = [] (juce::Point<float> c, float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c); };

    // Printed dial scale on the chassis; the centre tick is longer on bipolar controls.
    constexpr int ticks = 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (ticks - 1);
        const bool major = i == 0 || i == ticks - 1 || (bipolar && i == ticks / 2);
        g.setColour (major ? colours::textDim : colours::textFaint);
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius * (major ? 0.86f : 0.9f), a),
                                       centre.getPointOnCircumference (radius, a)), major ? 1.4f : 1.0f);
    }

    // Solid cylinder: drop shadow, side wall, ribbed skirt, raised centre cap.
    const auto cap = capColour (slider, colours::keyLight);
    const bool light = isLight (cap);
    const float sr = radius * 0.76f;                     // skirt radius
    const float wall = juce::jmax (2.0f, sr * 0.14f);    // visible height of the cylinder wall
    const auto skirtCol = cap.darker (light ? 0.22f : 0.15f);

    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillEllipse (circle (centre.translated (0.0f, wall + sr * 0.12f), sr * 1.06f));
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (circle (centre.translated (0.0f, wall + 1.0f), sr * 1.0f));

    // Cylinder wall, seen below the top surface.
    juce::Path side;
    side.addEllipse (circle (centre.translated (0.0f, wall), sr));
    side.addRectangle (centre.x - sr, centre.y, sr * 2.0f, wall);
    g.setColour (skirtCol.darker (light ? 0.7f : 0.6f));
    g.fillPath (side);

    // Skirt top with knurled ribs; the ribs turn with the knob.
    g.setGradientFill (juce::ColourGradient (skirtCol.brighter (0.06f), centre.x, centre.y - sr,
                                             skirtCol.darker (0.08f), centre.x, centre.y + sr, false));
    g.fillEllipse (circle (centre, sr));
    const int ribs = juce::jlimit (16, 32, (int) (sr * 1.4f));
    for (int i = 0; i < ribs; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) ribs;
        const auto outer = centre.getPointOnCircumference (sr - 0.6f, a);
        const auto inner = centre.getPointOnCircumference (sr * 0.8f, a);
        const auto lit = centre.getPointOnCircumference (sr - 0.6f, a + 0.06f);
        g.setColour (juce::Colours::black.withAlpha (light ? 0.22f : 0.45f));
        g.drawLine (juce::Line<float> (inner, outer), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (light ? 0.35f : 0.1f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (sr * 0.8f, a + 0.06f), lit), 0.7f);
    }

    // Raised centre cap: its own contact shadow, then a softly lit top.
    const float cr = sr * 0.7f;
    g.setColour (juce::Colours::black.withAlpha (light ? 0.25f : 0.4f));
    g.fillEllipse (circle (centre.translated (0.0f, 1.2f), cr + 0.5f));
    g.setGradientFill (juce::ColourGradient (cap.brighter (light ? 0.05f : 0.2f), centre.x - cr * 0.5f, centre.y - cr * 0.7f,
                                             cap.darker (light ? 0.06f : 0.1f), centre.x + cr * 0.5f, centre.y + cr * 0.8f, false));
    g.fillEllipse (circle (centre, cr));
    {
        // Rim: highlight on the upper half, shading on the lower half.
        juce::Path upper, lower;
        upper.addCentredArc (centre.x, centre.y, cr - 0.5f, cr - 0.5f, 0.0f, -2.2f, 0.9f, true);
        lower.addCentredArc (centre.x, centre.y, cr - 0.5f, cr - 0.5f, 0.0f, 1.4f, 4.4f, true);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (upper, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::black.withAlpha (light ? 0.12f : 0.3f));
        g.strokePath (lower, juce::PathStrokeType (1.0f));
    }

    // Indicator notch: a high-contrast line cut from the cap centre out across the skirt.
    const auto notchColour = light ? colours::ink : colours::keyText;
    juce::Path notch;
    notch.startNewSubPath (centre.getPointOnCircumference (cr * 0.25f, angle));
    notch.lineTo (centre.getPointOnCircumference (sr - 1.0f, angle));
    g.setColour (notchColour.withMultipliedAlpha (enabled ? 1.0f : 0.4f));
    g.strokePath (notch, juce::PathStrokeType (juce::jmax (2.0f, radius * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        // Recessed groove with printed ticks either side, and a chunky cap
        // that overhangs the groove.
        const auto slot = juce::Rectangle<float> (7.0f, r.getHeight() - 12.0f).withCentre (r.getCentre());
        drawGroove (g, slot);

        g.setColour (colours::textFaint);
        for (int i = 0; i <= 8; ++i)
        {
            const float ty = slot.getY() + slot.getHeight() * (float) i / 8.0f;
            const float len = i % 4 == 0 ? 4.0f : 2.5f;
            g.fillRect (slot.getX() - 3.0f - len, ty - 0.5f, len, 1.0f);
            g.fillRect (slot.getRight() + 3.0f, ty - 0.5f, len, 1.0f);
        }

        const auto capBounds = juce::Rectangle<float> (juce::jmin (r.getWidth() - 2.0f, 28.0f), 17.0f)
                                   .withCentre ({ r.getCentreX(), juce::jlimit (slot.getY(), slot.getBottom(), pos) });
        drawFaderCap (g, capBounds, true, enabled);
        return;
    }

    // Horizontal: groove with an orange value fill and an overhanging cap.
    const auto slot = juce::Rectangle<float> (r.getWidth() - 10.0f, 6.0f).withCentre (r.getCentre());
    drawGroove (g, slot);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (slot.withRight (juce::jmax (slot.getX(), pos)).reduced (1.0f, 2.0f), 1.0f);
    drawFaderCap (g, juce::Rectangle<float> (12.0f, juce::jmin (r.getHeight(), 20.0f)).withCentre ({ pos, r.getCentreY() }), false, enabled);
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

KeyState FloorplanLookAndFeel::keyState (juce::Button& b, bool down)
{
    // Latched toggles sit part way down, like a key held in.
    return down ? KeyState::down : (b.getToggleState() ? KeyState::latched : KeyState::up);
}

void FloorplanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    drawKey (g, b.getLocalBounds().toFloat(), faceColour (b), keyState (b, down), over && b.isEnabled());
}

void FloorplanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto face = keyFace (b.getLocalBounds().toFloat(), keyState (b, down));
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
