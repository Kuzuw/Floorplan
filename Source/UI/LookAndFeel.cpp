#include "UI/LookAndFeel.h"

namespace fp::ui
{

juce::Rectangle<int> drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto r = area.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto content = area.reduced (10, 8);
    if (title.isNotEmpty())
    {
        auto header = content.removeFromTop (18);
        g.setColour (colours::textDim);
        g.setFont (font (11.0f, true));
        g.drawText (title.toUpperCase(), header, juce::Justification::centredLeft);
        content.removeFromTop (4);
    }
    return content;
}

FloorplanLookAndFeel::FloorplanLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, textDim);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff17120a));
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff17120a));
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, borderLight);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, border);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
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
    const float trackWidth = juce::jmax (2.0f, radius * 0.13f);
    const float arcRadius = radius - trackWidth * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) slider.getProperties()["bipolar"];
    const bool enabled = slider.isEnabled();

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::well);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? colours::accent : colours::textFaint);
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cap
    const float capRadius = arcRadius - trackWidth * 1.3f;
    juce::ColourGradient grad (colours::raised.brighter (0.15f), centre.x, centre.y - capRadius,
                               colours::raised.darker (0.3f), centre.x, centre.y + capRadius, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));
    g.setColour (colours::border);
    g.drawEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre), 1.0f);

    // Pointer
    const auto tip = centre.getPointOnCircumference (capRadius * 0.85f, angle);
    const auto inner = centre.getPointOnCircumference (capRadius * 0.3f, angle);
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.drawLine ({ inner, tip }, juce::jmax (1.5f, radius * 0.07f));
}

void FloorplanLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();

    if (style == juce::Slider::LinearVertical)
    {
        const float trackW = 4.0f;
        const auto track = juce::Rectangle<float> (trackW, r.getHeight()).withCentre (r.getCentre());
        g.setColour (colours::well);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (colours::accent.withAlpha (slider.isEnabled() ? 0.9f : 0.3f));
        g.fillRoundedRectangle (track.withTop (pos), 2.0f);

        const auto thumb = juce::Rectangle<float> (juce::jmin (r.getWidth(), 26.0f), 12.0f).withCentre ({ r.getCentreX(), pos });
        g.setColour (colours::raised.brighter (0.25f));
        g.fillRoundedRectangle (thumb, 2.5f);
        g.setColour (colours::borderLight);
        g.drawRoundedRectangle (thumb, 2.5f, 1.0f);
        g.setColour (colours::text);
        g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 8.0f, 1.5f));
        return;
    }

    // Horizontal: thin bar
    const auto track = juce::Rectangle<float> (r.getWidth(), 4.0f).withCentre (r.getCentre());
    g.setColour (colours::well);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ pos, r.getCentreY() }));
}

void FloorplanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto onColour = b.findColour (juce::TextButton::buttonOnColourId);
    auto base = on ? onColour : b.findColour (juce::TextButton::buttonColourId);
    if (! b.isEnabled()) base = base.withMultipliedAlpha (0.4f);
    if (down) base = base.darker (0.2f);
    else if (over) base = base.brighter (on ? 0.1f : 0.08f);

    g.setColour (base);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? onColour.brighter (0.2f) : colours::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void FloorplanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.5f);
    g.setColour (colour);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 1), juce::Justification::centred, 1, 0.8f);
}

void FloorplanLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::borderLight : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

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
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
    g.setColour (colours::borderLight);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
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
