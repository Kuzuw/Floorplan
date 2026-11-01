#include "UI/SoundDesignComponent.h"
#include "Parameters/ParameterIDs.h"

namespace fp::ui
{

namespace
{
constexpr const char* kEngineNames[] = { "SINE BODY + PITCH SWEEP + CLICK", "TWO-MODE SHELL + NOISE WIRES",
                                         "BAND-PASSED BURST NOISE", "6-OSC METALLIC + NOISE", "TUNED FM PERCUSSION" };
constexpr int kPreviewWidth = 240;
} // namespace

SoundDesignComponent::SoundDesignComponent (FloorplanProcessor& p, UiState& s) : proc (p), state (s)
{
    for (int i = 0; i < kNumSynthParams; ++i)
    {
        auto* k = synthKnobs.add (new Knob (proc));
        k->setBarStyle (true);
        addAndMakeVisible (k);
    }

    for (auto* k : { &filterKnob, &driveKnob, &levelKnob, &panKnob, &sampleTuneKnob, &sampleDecayKnob })
        addAndMakeVisible (k);

    sampleButton.setTooltip ("Replace the synthesis of this voice with a WAV/AIFF sample (or drag a file onto the instrument)");
    sampleButton.onClick = [this] { if (onLoadSample) onLoadSample (state.voice); };
    clearSampleButton.setTooltip ("Remove the sample and return to synthesis");
    clearSampleButton.onClick = [this] { proc.clearSample (state.voice); };
    auditionButton.setTooltip ("Audition this voice");
    auditionButton.onClick = [this] { proc.triggerVoice (state.voice, 0.85f); };
    addAndMakeVisible (sampleButton);
    addChildComponent (clearSampleButton);
    addAndMakeVisible (auditionButton);
    setKeyStyle (auditionButton, KeyStyle::orange);
    setKeyStyle (clearSampleButton, KeyStyle::dark);
    levelKnob.setCap (colours::keyDark);
    outputBox.setTooltip ("Output routing: Main mix or a separate stereo output (enable the outputs in your DAW)");
    addAndMakeVisible (outputBox);

    state.addChangeListener (this);
    bindVoice();
}

SoundDesignComponent::~SoundDesignComponent()
{
    state.removeChangeListener (this);
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
        for (auto* k : synthKnobs) k->setEnabled (! sample);
        sampleTuneKnob.setVisible (sample);
        sampleDecayKnob.setVisible (sample);
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

void SoundDesignComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (22); // title row

    auto top = r.removeFromTop (84);
    previewArea = top.removeFromRight (juce::jmin (kPreviewWidth, top.getWidth() / 2));
    top.removeFromRight (10);
    auto buttons = top.removeFromBottom (24);
    auto routing = top.removeFromTop (24);
    routing.removeFromLeft (52); // "OUTPUT" label
    outputBox.setBounds (routing.removeFromLeft (96));
    infoArea = routing.withTrimmedLeft (10);
    auditionButton.setBounds (buttons.removeFromLeft (52));
    buttons.removeFromLeft (6);
    if (shownSample)
    {
        clearSampleButton.setBounds (buttons.removeFromRight (24));
        buttons.removeFromRight (4);
    }
    sampleButton.setBounds (buttons.withWidth (juce::jmin (buttons.getWidth(), 200)));

    r.removeFromTop (8);
    auto row1 = r.removeFromTop (juce::roundToInt ((float) r.getHeight() * 0.6f));
    const int knobW = row1.getWidth() / kNumSynthParams;
    for (auto* k : synthKnobs)
        k->setBounds (row1.removeFromLeft (knobW).reduced (3, 0));

    auto row2 = r;
    const int w2 = row2.getWidth() / kNumSynthParams;
    for (auto* k : { &filterKnob, &driveKnob, &levelKnob, &panKnob })
        k->setBounds (row2.removeFromLeft (w2).reduced (4, 2));
    sampleTuneKnob.setBounds (row2.removeFromLeft (w2).reduced (4, 2));
    sampleDecayKnob.setBounds (row2.removeFromLeft (w2).reduced (4, 2));
}

void SoundDesignComponent::paint (juce::Graphics& g)
{
    const auto& spec = kVoiceSpecs[(size_t) boundVoice];
    drawSectionTitle (g, drawPanel (g, getLocalBounds(), {}), spec.name,
                      shownSample ? juce::String ("Sample playback") : juce::String (kEngineNames[(int) spec.engine]));

    // Routing and MIDI info row
    g.setColour (colours::textDim);
    g.setFont (font (9.5f, true).withExtraKerningFactor (0.06f));
    g.drawText ("OUTPUT", outputBox.getBounds().withX (outputBox.getX() - 52).withWidth (48), juce::Justification::centredLeft);
    const int note = proc.getVoiceNote (boundVoice);
    g.drawText ("MIDI " + juce::MidiMessage::getMidiNoteName (note, true, true, 3), infoArea, juce::Justification::centredLeft);

    // Waveform preview
    // Waveform in its own display window.
    const auto pr = drawLcd (g, previewArea.toFloat()).reduced (2.0f, 4.0f);
    g.setColour (colours::lcdGhost);
    g.fillRect (pr.getX(), pr.getCentreY(), pr.getWidth(), 1.0f);
    if (! previewMin.empty() && ! shownSample)
    {
        const float mid = pr.getCentreY();
        const float scale = pr.getHeight() * 0.45f;
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
        g.setColour (colours::lcdText.withAlpha (0.12f));
        g.fillPath (wave);
        g.setColour (colours::lcdText);
        g.strokePath (wave, juce::PathStrokeType (1.0f));
    }
    g.setColour (colours::lcdDim);
    g.setFont (monoFont (9.0f, true));
    g.drawText (shownSample ? juce::String ("SAMPLE") : juce::String (juce::roundToInt (previewSeconds * 1000.0f)) + " MS",
                previewArea.reduced (8, 6), juce::Justification::topRight);
    g.setColour (colours::accent);
    g.drawText ("SCOPE " + juce::String (spec.shortName), previewArea.reduced (8, 6), juce::Justification::topLeft);
}

} // namespace fp::ui
