#include "UI/LookAndFeel.h"

namespace fp::ui
{

namespace
{
constexpr float kCorner = 2.0f;
}

juce::Rectangle<int> drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& subtitle)
{
    auto header = area.removeFromTop (18);
    g.setColour (colours::text);
    g.setFont (font (14.5f, true));
    const auto upper = title.toUpperCase();
    g.drawText (upper, header, juce::Justification::centredLeft);

    if (subtitle.isNotEmpty())
    {
        const int titleW = juce::GlyphArrangement::getStringWidthInt (font (14.5f, true), upper);
        g.setColour (colours::textFaint);
        g.setFont (font (10.5f, true));
        g.drawText (subtitle.toUpperCase(), header.withTrimmedLeft (titleW + 12), juce::Justification::centredLeft);
    }

    // Dashed rule under the title, like silkscreen on a hardware panel.
    const float y = (float) area.getY() + 0.5f;
    const float dashes[] = { 3.0f, 3.0f };
    g.setColour (colours::borderLight);
    g.drawDashedLine ({ (float) area.getX(), y, (float) area.getRight(), y }, dashes, 2, 1.0f);

    area.removeFromTop (4);
    return area;
}

juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto r = area.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, kCorner);
    g.setColour (colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), kCorner, 1.0f);

    auto content = area.reduced (10, 8);
    if (title.isNotEmpty())
        content = drawSectionTitle (g, content, title);
    return content;
}

FloorplanLookAndFeel::FloorplanLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOnId, ink);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, borderLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, borderLight);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, borderLight);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::textColourId, text);
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
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = juce::jmax (2.0f, radius * 0.09f);
    const float arcRadius = radius - trackWidth * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::border);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // Value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? colours::accent : colours::textFaint);
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // Flat cap with a fine outline
    const float capRadius = arcRadius - trackWidth * 2.2f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (colours::raised);
    g.fillEllipse (cap);
    g.setColour (colours::borderLight);
    g.drawEllipse (cap, 1.0f);

    // Pointer
    const auto tip = centre.getPointOnCircumference (capRadius * 0.9f, angle);
    const auto inner = centre.getPointOnCircumference (capRadius * 0.25f, angle);
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.drawLine ({ inner, tip }, juce::jmax (1.5f, radius * 0.07f));
}

void FloorplanLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool enabled = slider.isEnabled();
    const auto fillColour = enabled ? colours::accent : colours::textFaint.withAlpha (0.5f);

    if (style == juce::Slider::LinearBarVertical)
    {
        // Tall outlined bar filled from the bottom (or from the centre when
        // bipolar), with a bright edge at the value.
        const auto box = r.reduced (0.5f);
        g.setColour (colours::well);
        g.fillRect (box);
        const bool bipolar = (bool) slider.getProperties()["bipolar"];
        const float base = bipolar ? box.getCentreY() : box.getBottom();
        const float top = juce::jlimit (box.getY(), box.getBottom(), pos);
        auto fill = juce::Rectangle<float>::leftTopRightBottom (box.getX(), juce::jmin (top, base), box.getRight(), juce::jmax (top, base));
        g.setColour (fillColour.withMultipliedAlpha (bipolar ? 0.85f : 0.92f));
        g.fillRect (fill.getIntersection (box.reduced (2.0f)));
        g.setColour (enabled ? colours::text : colours::textFaint);
        g.fillRect (box.getX() + 2.0f, top - 1.0f, box.getWidth() - 4.0f, 2.0f);
        if (bipolar)
        {
            g.setColour (colours::borderLight);
            g.fillRect (box.getX(), base - 0.5f, box.getWidth(), 1.0f);
        }
        g.setColour (colours::borderLight);
        g.drawRect (box, 1.0f);
        return;
    }

    if (style == juce::Slider::LinearVertical)
    {
        // Fader: outlined slot filled below the cap.
        const auto slot = juce::Rectangle<float> (juce::jmin (8.0f, r.getWidth()), r.getHeight()).withCentre (r.getCentre());
        g.setColour (colours::well);
        g.fillRect (slot);
        g.setColour (fillColour.withMultipliedAlpha (0.8f));
        g.fillRect (slot.withTop (pos).reduced (1.5f, 0.0f));
        g.setColour (colours::border);
        g.drawRect (slot, 1.0f);

        const auto thumb = juce::Rectangle<float> (juce::jmin (r.getWidth(), 24.0f), 9.0f).withCentre ({ r.getCentreX(), pos });
        g.setColour (enabled ? colours::accent : colours::textFaint);
        g.fillRect (thumb);
        g.setColour (colours::ink);
        g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 8.0f, 1.0f));
        return;
    }

    // Horizontal: thin line with a square cap.
    const auto line = juce::Rectangle<float> (r.getWidth(), 2.0f).withCentre (r.getCentre());
    g.setColour (colours::border);
    g.fillRect (line);
    g.setColour (fillColour);
    g.fillRect (line.withRight (pos));
    g.fillRect (juce::Rectangle<float> (8.0f, 12.0f).withCentre ({ pos, r.getCentreY() }));
}

void FloorplanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    auto base = on ? onColour : b.findColour (juce::TextButton::buttonColourId);
    if (! b.isEnabled()) base = base.withMultipliedAlpha (0.4f);
    if (down) base = on ? base.darker (0.15f) : base.brighter (0.15f);
    else if (over) base = on ? base.brighter (0.08f) : base.brighter (0.1f);

    g.setColour (base);
    g.fillRoundedRectangle (r, kCorner);
    g.setColour (on ? onColour : (b.isEnabled() ? (over ? colours::textDim : colours::borderLight) : colours::border));
    g.drawRoundedRectangle (r, kCorner, 1.0f);
}

void FloorplanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.4f);
    g.setColour (colour);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.8f);
}

void FloorplanLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, kCorner);
    g.setColour (box.hasKeyboardFocus (true) ? colours::textDim : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, kCorner, 1.0f);

    if (w < 44)
        return; // compact boxes (e.g. chain slots) show only their value

    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.3f));
    g.fillPath (arrow);
}

void FloorplanLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const bool compact = box.getWidth() < 44;
    label.setBounds (compact ? juce::Rectangle<int> (1, 0, box.getWidth() - 2, box.getHeight())
                             : juce::Rectangle<int> (4, 0, box.getWidth() - 22, box.getHeight()));
    label.setJustificationType (compact ? juce::Justification::centred : juce::Justification::centredLeft);
    label.setFont (getComboBoxFont (box));
}

void FloorplanLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (colours::raised);
    g.fillRect (0, 0, w, h);
    g.setColour (colours::borderLight);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

void FloorplanLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (colours::raised);
    g.fillPath (path);
    g.setColour (colours::borderLight);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

} // namespace fp::ui
