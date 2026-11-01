#include "UI/SoundDesignComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

namespace
{
constexpr const char* kEngineNames[] = { "Sine body + pitch sweep + click", "Two-mode shell + noise wires",
                                         "Band-passed burst noise", "6-osc metallic + noise", "Tuned FM percussion" };
constexpr const char* kTabNames[] = { "SOURCE", "TONE", "OUTPUT" };
constexpr int kPreviewWidth = 240;
constexpr int kTitleHeight = 24;
constexpr int kKnobRowHeight = 92;
} // namespace

SoundDesignComponent::SoundDesignComponent (FloorplanProcessor& p, UiState& s) : proc (p), state (s)
{
    for (int i = 0; i < kNumSynthParams; ++i)
        addChildComponent (synthKnobs.add (new Knob (proc)));

    for (auto* k : { &filterKnob, &driveKnob, &levelKnob, &panKnob, &sampleTuneKnob, &sampleDecayKnob })
        addChildComponent (k);

    for (int t = 0; t < NumTabs; ++t)
    {
        auto* b = tabButtons.add (new juce::TextButton (kTabNames[t]));
        b->setRadioGroupId (5150);
        b->setClickingTogglesState (true);
        b->setToggleState (t == tab, juce::dontSendNotification);
        b->onClick = [this, t] { tab = t; resized(); repaint(); };
        addChildComponent (b);
    }

    sampleButton.setTooltip ("Replace the synthesis of this voice with a WAV/AIFF/FLAC sample (or drag a file onto the instrument)");
    sampleButton.onClick = [this] { if (onLoadSample) onLoadSample (state.voice); };
    clearSampleButton.setTooltip ("Remove the sample and return to synthesis");
    clearSampleButton.onClick = [this] { proc.clearSample (state.voice); };
    auditionButton.setTooltip ("Audition this voice");
    auditionButton.onClick = [this] { proc.triggerVoice (state.voice, 0.85f); };
    addAndMakeVisible (sampleButton);
    addChildComponent (clearSampleButton);
    addAndMakeVisible (auditionButton);
    setKeyStyle (auditionButton, KeyStyle::orange);
    outputBox.setTooltip ("Output routing: Main mix or a separate stereo output (enable the outputs in your DAW)");
    addChildComponent (outputBox);

    state.addChangeListener (this);
    bindVoice();
}

SoundDesignComponent::~SoundDesignComponent()
{
    state.removeChangeListener (this);
}

void SoundDesignComponent::setAdvanced (bool shouldBeAdvanced)
{
    if (advanced == shouldBeAdvanced)
        return;
    advanced = shouldBeAdvanced;
    resized();
    repaint();
}

void SoundDesignComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (state.voice != boundVoice)
        bindVoice();
}

void SoundDesignComponent::bindVoice()
{
    const int v = state.voice;
    boundVoice = v;
    const auto& spec = kVoiceSpecs[(size_t) v];

    for (int i = 0; i < kNumSynthParams; ++i)
        synthKnobs[i]->bind (ids::synth (v, i), spec.params[(size_t) i].name);

    filterKnob.bind (ids::filter (v), "Filter", true);
    driveKnob.bind (ids::drive (v), "Drive");
    levelKnob.bind (ids::level (v), "Level");
    panKnob.bind (ids::pan (v), "Pan", true);
    sampleTuneKnob.bind (ids::sampleTune (v), "Smp Tune", true);
    sampleDecayKnob.bind (ids::sampleDecay (v), "Smp Decay");
    outputBox.bind (ids::output (v));

    previewHash = -1.0f;
    shownSample = ! shownSample; // force layout refresh
    refresh();
    repaint();
}

float SoundDesignComponent::parameterHash() const
{
    float h = (float) boundVoice * 1000.0f;
    for (int i = 0; i < kNumSynthParams; ++i)
        h = h * 1.37f + proc.apvts.getRawParameterValue (ids::synth (boundVoice, i))->load();
    return h;
}

void SoundDesignComponent::refresh()
{
    const bool sample = proc.getSamplePath (boundVoice).isNotEmpty();
    if (sample != shownSample)
    {
        shownSample = sample;
        clearSampleButton.setVisible (sample);
        sampleButton.setButtonText (sample ? juce::File (proc.getSamplePath (boundVoice)).getFileNameWithoutExtension() : "LOAD SAMPLE");
        resized();
        repaint();
    }

    const int note = proc.getVoiceNote (boundVoice);
    if (note != shownNote)
    {
        shownNote = note;
        repaint (infoArea);
    }

    const float h = parameterHash();
    if (h != previewHash)
    {
        previewHash = h;
        renderPreview();
        repaint (previewArea);
    }
}

void SoundDesignComponent::renderPreview()
{
    // Render the real voice DSP offline. A few milliseconds of work on the
    // message thread, only when a synthesis parameter changes.
    auto voice = createSynthVoice (boundVoice);
    constexpr double sr = 24000.0;
    voice->prepare (sr);

    std::array<float, kNumSynthParams> params {};
    for (int i = 0; i < kNumSynthParams; ++i)
        params[(size_t) i] = proc.apvts.getRawParameterValue (ids::synth (boundVoice, i))->load();

    // Show roughly the audible length, between 150 ms and 2.5 s.
    float decayMs = 300.0f;
    for (int i = 0; i < kNumSynthParams; ++i)
        if (juce::String (kVoiceSpecs[(size_t) boundVoice].params[(size_t) i].id) == "decay")
            decayMs = params[(size_t) i];
    previewSeconds = juce::jlimit (0.15f, 2.5f, decayMs * 0.001f * 1.1f);

    voice->trigger (params.data(), TriggerInfo {});
    const int total = (int) (previewSeconds * sr);
    std::vector<float> L ((size_t) total), R ((size_t) total);
    voice->render (L.data(), R.data(), total);

    previewMin.assign (kPreviewWidth, 0.0f);
    previewMax.assign (kPreviewWidth, 0.0f);
    for (int i = 0; i < total; ++i)
    {
        const int x = juce::jmin (kPreviewWidth - 1, i * kPreviewWidth / total);
        previewMin[(size_t) x] = juce::jmin (previewMin[(size_t) x], L[(size_t) i]);
        previewMax[(size_t) x] = juce::jmax (previewMax[(size_t) x], L[(size_t) i]);
    }
}

void SoundDesignComponent::mouseDown (const juce::MouseEvent& e)
{
    if (previewArea.contains (e.getPosition()))
        proc.triggerVoice (state.voice, 0.85f);
}

std::vector<juce::Component*> SoundDesignComponent::visibleControls()
{
    // A sampled voice has no synthesis controls; its sample controls take their place.
    std::vector<juce::Component*> source;
    if (shownSample)
        source = { &sampleTuneKnob, &sampleDecayKnob };
    else
        for (auto* k : synthKnobs)
            source.push_back (k);

    if (! advanced)
    {
        source.push_back (&filterKnob);
        source.push_back (&driveKnob);
        return source;
    }

    switch (tab)
    {
        case TabTone:   return { &filterKnob, &driveKnob };
        case TabOutput: return { &levelKnob, &panKnob, &outputBox };
        case TabSource:
        default:        return source;
    }
}

void SoundDesignComponent::layoutControls (juce::Rectangle<int> area, const std::vector<juce::Component*>& controls)
{
    // One row of equal cells; the cell width is capped so a short list does not
    // produce huge knobs. Combo boxes sit centred in their cell.
    const int n = (int) controls.size();
    if (n == 0)
        return;
    const int cellW = juce::jmin (84, area.getWidth() / n);
    auto row = area.withHeight (juce::jmin (area.getHeight(), kKnobRowHeight));
    for (auto* c : controls)
    {
        auto cell = row.removeFromLeft (dynamic_cast<juce::ComboBox*> (c) != nullptr ? 128 : cellW);
        if (dynamic_cast<juce::ComboBox*> (c) != nullptr)
            c->setBounds (cell.withSizeKeepingCentre (cell.getWidth() - 12, 26).translated (0, 4));
        else
            c->setBounds (cell.reduced (3, 0));
        c->setVisible (true);
    }
}

void SoundDesignComponent::resized()
{
    for (auto* k : synthKnobs) k->setVisible (false);
    for (auto* c : std::initializer_list<juce::Component*> { &filterKnob, &driveKnob, &levelKnob, &panKnob,
                                                              &sampleTuneKnob, &sampleDecayKnob, &outputBox })
        c->setVisible (false);
    for (auto* b : tabButtons)
        b->setVisible (advanced);

    auto r = getLocalBounds().reduced (12, 10);
    auto title = r.removeFromTop (kTitleHeight);
    if (advanced)
        for (int t = NumTabs; --t >= 0;)
        {
            tabButtons[t]->setBounds (title.removeFromRight (70).withHeight (20));
            title.removeFromRight (4);
        }

    const auto controls = visibleControls();
    dividerX = -1;

    if (! advanced)
    {
        // Preview and buttons on top, one row of controls along the bottom.
        auto knobs = r.removeFromBottom (kKnobRowHeight);
        r.removeFromBottom (8);
        previewArea = r.removeFromLeft (juce::jmin (kPreviewWidth + 20, r.getWidth() * 3 / 5));
        r.removeFromLeft (12);
        auto side = r;
        auditionButton.setBounds (side.removeFromTop (28).withWidth (64));
        side.removeFromTop (6);
        auto sampleRow = side.removeFromTop (28);
        if (shownSample)
        {
            clearSampleButton.setBounds (sampleRow.removeFromRight (28));
            sampleRow.removeFromRight (4);
        }
        sampleButton.setBounds (sampleRow);
        infoArea = side.removeFromTop (22);

        layoutControls (knobs, controls);
        const int numSource = (int) controls.size() - 2;
        if (numSource > 0)
            dividerX = controls[(size_t) numSource]->getX() - 3;
        return;
    }

    // Advanced: preview column on the left, the selected tab's controls on the right.
    auto column = r.removeFromLeft (250);
    r.removeFromLeft (16);
    auto buttons = column.removeFromBottom (28);
    column.removeFromBottom (8);
    infoArea = column.removeFromBottom (18);
    previewArea = column;
    auditionButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (6);
    if (shownSample)
    {
        clearSampleButton.setBounds (buttons.removeFromRight (28));
        buttons.removeFromRight (4);
    }
    sampleButton.setBounds (buttons);

    layoutControls (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), kKnobRowHeight)).withY (r.getY() + 8), controls);
}

void SoundDesignComponent::paint (juce::Graphics& g)
{
    const auto& spec = kVoiceSpecs[(size_t) boundVoice];
    const auto content = drawPanel (g, getLocalBounds(), {});

    // Title: family badge, voice name and its engine.
    auto title = content.withHeight (kTitleHeight);
    drawBadge (g, title.removeFromLeft (28).toFloat().withSizeKeepingCentre (28.0f, 18.0f).translated (0.0f, -3.0f), boundVoice);
    title.removeFromLeft (8);
    if (advanced)
        title.setRight (tabButtons[0]->getX() - 8);
    drawSectionTitle (g, title, spec.name, shownSample ? juce::String ("Sample playback") : juce::String (kEngineNames[(int) spec.engine]));

    // MIDI note of the voice.
    g.setColour (colours::grey);
    g.setFont (labelFont (10.0f));
    g.drawText ("MIDI NOTE " + juce::MidiMessage::getMidiNoteName (proc.getVoiceNote (boundVoice), true, true, 3),
                infoArea, juce::Justification::centredLeft);

    if (outputBox.isVisible())
    {
        g.setColour (colours::ink);
        g.setFont (labelFont());
        g.drawText ("OUTPUT", outputBox.getBounds().withY (outputBox.getY() - 22).withHeight (15), juce::Justification::centred);
    }

    if (dividerX > 0)
    {
        g.setColour (colours::line);
        g.fillRect (dividerX, filterKnob.getY() + 4, 1, filterKnob.getHeight() - 8);
    }

    // Waveform in its own display window.
    const auto pr = drawScreen (g, previewArea.toFloat()).reduced (4.0f, 6.0f);
    g.setColour (colours::screenGhost);
    g.fillRect (pr.getX(), pr.getCentreY(), pr.getWidth(), 1.0f);
    if (! previewMin.empty() && ! shownSample)
    {
        const float mid = pr.getCentreY();
        const float scale = pr.getHeight() * 0.42f;
        juce::Path wave;
        const int n = (int) previewMin.size();
        for (int x = 0; x < n; ++x)
        {
            const float px = pr.getX() + pr.getWidth() * (float) x / (float) (n - 1);
            const float y = mid - previewMax[(size_t) x] * scale;
            x == 0 ? wave.startNewSubPath (px, y) : wave.lineTo (px, y);
        }
        for (int x = n - 1; x >= 0; --x)
            wave.lineTo (pr.getX() + pr.getWidth() * (float) x / (float) (n - 1), mid - previewMin[(size_t) x] * scale);
        wave.closeSubPath();
        g.setColour (colours::screenText.withAlpha (0.12f));
        g.fillPath (wave);
        g.setColour (colours::screenText);
        g.strokePath (wave, juce::PathStrokeType (1.0f));
    }
    g.setColour (colours::screenDim);
    g.setFont (monoFont (9.5f, true));
    g.drawText (shownSample ? juce::String ("SAMPLE") : juce::String (juce::roundToInt (previewSeconds * 1000.0f)) + " MS",
                previewArea.reduced (8, 6), juce::Justification::topRight);
    g.setColour (colours::accent);
    g.drawText ("SCOPE " + juce::String (spec.shortName), previewArea.reduced (8, 6), juce::Justification::topLeft);
}

} // namespace fp::ui
