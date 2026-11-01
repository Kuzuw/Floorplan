#include "UI/Widgets.h"

namespace fp::ui
{

void showParameterMenu (FloorplanProcessor& proc, const juce::String& paramId, juce::Component& target)
{
    auto* param = proc.apvts.getParameter (paramId);
    if (param == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (param->getName (64));
    menu.addItem (1, "Reset to default");
    menu.addSeparator();
    menu.addItem (2, "MIDI Learn (move a controller)");
    const int cc = proc.getCcForParameter (paramId);
    menu.addItem (3, cc >= 0 ? "Clear MIDI CC " + juce::String (cc) : juce::String ("No MIDI CC assigned"), cc >= 0);

    juce::Component::SafePointer<juce::Component> safe (&target);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [&proc, paramId, safe] (int result)
                        {
                            auto* p = proc.apvts.getParameter (paramId);
                            if (p == nullptr || safe == nullptr)
                                return;
                            if (result == 1)
                            {
                                p->beginChangeGesture();
                                p->setValueNotifyingHost (p->getDefaultValue());
                                p->endChangeGesture();
                            }
                            else if (result == 2) proc.beginCcLearn (paramId);
                            else if (result == 3) proc.clearCcMapping (paramId);
                        });
}

// ---------------------------------------------------------------------------
Knob::Knob (FloorplanProcessor& p, const juce::String& id, const juce::String& text, bool bipolar) : proc (p)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.onDragStart = [this] { dragging = true; repaint(); };
    slider.onDragEnd = [this] { dragging = false; repaint(); };
    slider.onValueChange = [this] { if (layout == Layout::full || hovering || dragging) repaint(); };
    slider.onRightClick = [this] { if (paramId.isNotEmpty()) showParameterMenu (proc, paramId, *this); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);

    if (id.isNotEmpty())
        bind (id, text, bipolar);
}

Knob::~Knob()
{
    slider.removeMouseListener (this);
}

void Knob::bind (const juce::String& id, const juce::String& text, bool bipolar)
{
    attachment.reset();
    paramId = id;
    label = text;
    slider.getProperties().set ("bipolar", bipolar);

    if (auto* p = proc.apvts.getParameter (id))
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, slider);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        setTooltip (p->getName (64));
        slider.setEnabled (true);
    }
    repaint();
}

void Knob::unbind()
{
    attachment.reset();
    paramId.clear();
    label.clear();
    slider.setEnabled (false);
    repaint();
}

void Knob::setLayout (Layout newLayout)
{
    layout = newLayout;
    resized();
    repaint();
}

void Knob::setCap (juce::Colour capColour)
{
    slider.getProperties().set ("cap", (juce::int64) capColour.getARGB());
    slider.repaint();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (layout == Layout::full)
        r.removeFromTop (15);
    r.removeFromBottom (15);
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool enabled = slider.isEnabled();
    const bool hasValue = attachment != nullptr;
    const auto valueText = hasValue ? slider.getTextFromValue (slider.getValue()) : juce::String();

    if (layout == Layout::full)
    {
        g.setFont (labelFont());
        g.setColour (enabled ? colours::label : colours::greyLight);
        g.drawFittedText (label.toUpperCase(), r.removeFromTop (15), juce::Justification::centred, 1, 0.7f);
        g.setFont (monoFont (12.0f));
        g.setColour (dragging ? colours::accentText : (enabled ? colours::text : colours::greyLight));
        g.drawFittedText (valueText, r.removeFromBottom (15), juce::Justification::centred, 1, 0.6f);
        return;
    }

    const bool showValue = (hovering || dragging) && hasValue;
    g.setFont (showValue ? monoFont (11.5f) : labelFont (10.0f));
    g.setColour (showValue ? colours::accentText : (enabled ? colours::grey : colours::greyLight));
    g.drawFittedText (showValue ? valueText : label.toUpperCase(), r.removeFromBottom (15), juce::Justification::centred, 1, 0.6f);
}

// ---------------------------------------------------------------------------
ParamToggle::ParamToggle (FloorplanProcessor& p, const juce::String& text, juce::Colour onColour) : proc (p)
{
    setButtonText (text);
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, onColour);
}

void ParamToggle::bind (const juce::String& id)
{
    attachment.reset();
    paramId = id;
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *this);
}

void ParamToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showParameterMenu (proc, paramId, *this);
        return;
    }
    juce::TextButton::mouseDown (e);
}

// ---------------------------------------------------------------------------
ParamCombo::ParamCombo (FloorplanProcessor& p, const juce::String& id) : proc (p)
{
    if (id.isNotEmpty())
        bind (id);
}

void ParamCombo::bind (const juce::String& id)
{
    attachment.reset();
    clear (juce::dontSendNotification);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        addItemList (choice->choices, 1);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *this);
    }
}

// ---------------------------------------------------------------------------
void LevelMeter::setLevels (float left, float right)
{
    bool changed = false;
    const float in[2] = { left, right };
    for (int c = 0; c < numChannels; ++c)
    {
        // Instant attack, smooth ~20 dB/s fall at the 30 Hz editor refresh rate.
        const float target = juce::jmax (in[c], levels[c] * 0.89f);
        levels[c] = target < 1.0e-4f ? 0.0f : target;
        if (std::abs (levels[c] - shown[c]) > 0.002f)
        {
            shown[c] = levels[c];
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    // LED bar on a dark window: cream segments, orange in the top fifth, red
    // at clip. A stereo meter prints "L/R" up its right edge.
    auto window = getLocalBounds().toFloat();
    g.setColour (colours::screen);
    g.fillRoundedRectangle (window, 2.0f);

    auto inner = window.reduced (2.0f);
    if (numChannels == 2 && inner.getWidth() > 18.0f)
    {
        const auto legend = inner.removeFromRight (9.0f);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, legend.getCentreX(), legend.getCentreY()));
            g.setColour (colours::screenDim);
            g.setFont (labelFont (8.0f));
            g.drawText ("L/R", juce::Rectangle<float> (40.0f, legend.getWidth()).withCentre (legend.getCentre()), juce::Justification::centred);
        }
        inner.removeFromRight (2.0f);
    }

    const float gap = 2.0f;
    const float barW = (inner.getWidth() - gap * (float) (numChannels - 1)) / (float) numChannels;
    constexpr float segGap = 1.5f;
    const float segH = inner.getHeight() > 120.0f ? 3.0f : 2.0f;
    const int n = juce::jmax (1, (int) ((inner.getHeight() + segGap) / (segH + segGap)));

    for (int c = 0; c < numChannels; ++c)
    {
        // Map -48..+6 dB to the column height.
        const float db = shown[c] > 0.0f ? juce::Decibels::gainToDecibels (shown[c]) : -100.0f;
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f);
        const int lit = juce::roundToInt (norm * (float) n);
        const float x = inner.getX() + (float) c * (barW + gap);

        for (int i = 0; i < n; ++i)
        {
            const float segNorm = (float) (i + 1) / (float) n;
            const auto colour = segNorm > 0.95f ? colours::danger : (segNorm > 0.8f ? colours::accent : colours::screenText);
            g.setColour (i < lit ? colour : colours::screenGhost);
            g.fillRect (x, inner.getBottom() - (float) (i + 1) * (segH + segGap) + segGap, barW, segH);
        }
    }
}

} // namespace fp::ui
