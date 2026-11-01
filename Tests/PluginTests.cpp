// Tests for the JUCE plugin layer: processor, parameters, state and presets.

#include "TestFramework.h"

#include "PluginProcessor.h"
#include "Parameters/ParameterIDs.h"
#include "UI/MainComponent.h"
#include "Presets/FactoryContent.h"
#include "Presets/PatternSerializer.h"

#include <cstring>
#include <set>

using namespace fp;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct FakePlayHead final : juce::AudioPlayHead
{
    bool playing = true;
    double ppq = 0.0, bpm = 120.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

std::unique_ptr<FloorplanProcessor> makeProcessor()
{
    auto p = std::make_unique<FloorplanProcessor>();
    p->setRateAndBufferSizeDetails (kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    return p;
}

float processBlocks (FloorplanProcessor& p, int blocks, juce::MidiBuffer* firstMidi = nullptr, FakePlayHead* head = nullptr)
{
    juce::AudioBuffer<float> buffer (juce::jmax (2, p.getTotalNumOutputChannels()), kBlock);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0 && firstMidi != nullptr)
            midi = *firstMidi;
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = buffer.getSample (ch, i);
                if (! std::isfinite (s)) return -1.0f;
                peak = juce::jmax (peak, std::abs (s));
            }
        if (head != nullptr)
            head->ppq += kBlock * head->bpm / (60.0 * kRate);
    }
    return peak;
}

void setParam (FloorplanProcessor& p, const juce::String& id, float realValue)
{
    auto* param = p.apvts.getParameter (id);
    CHECK (param != nullptr);
    if (param != nullptr)
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

float getParam (FloorplanProcessor& p, const juce::String& id)
{
    return p.apvts.getRawParameterValue (id)->load();
}
} // namespace

FP_TEST (processor_produces_audio_from_midi)
{
    auto p = makeProcessor();
    CHECK (processBlocks (*p, 4) == 0.0f); // silent without input

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), 10);
    const float peak = processBlocks (*p, 20, &midi);
    CHECK (peak > 0.05f && peak <= 1.0f);
    CHECK (p->getTriggerCount (Kick) == 1);
}

FP_TEST (internal_transport_plays_default_pattern)
{
    auto p = makeProcessor();
    p->setInternalPlaying (true);
    const float peak = processBlocks (*p, (int) (kRate * 2.0 / kBlock));
    CHECK (peak > 0.05f && peak <= 1.0f);
    CHECK (p->getTriggerCount (Kick) >= 3);
    CHECK (p->isTransportRunning());

    p->setInternalPlaying (false);
    processBlocks (*p, 2);
    CHECK (p->getPlayingStep() == -1);
}

FP_TEST (host_transport_drives_sequencer)
{
    auto p = makeProcessor();
    FakePlayHead head;
    p->setPlayHead (&head);

    // One bar at 120 BPM = 2 s: the default pattern has four kicks.
    const int blocks = (int) std::ceil (kRate * 2.0 / kBlock) - 1;
    processBlocks (*p, blocks, nullptr, &head);
    CHECK (p->getTriggerCount (Kick) == 4);
    CHECK (p->isHostSynced());
    CHECK (std::abs (p->getDisplayBpm() - 120.0) < 1.0e-6);

    // Host stops: nothing more is triggered.
    head.playing = false;
    const auto before = p->getTriggerCount (Kick);
    processBlocks (*p, 100, nullptr, &head);
    CHECK (p->getTriggerCount (Kick) == before);
    p->setPlayHead (nullptr);
}

FP_TEST (state_round_trip)
{
    auto a = makeProcessor();
    setParam (*a, ids::synth (Kick, KickP::Tune), 61.0f);
    setParam (*a, ids::swing, 63.0f);
    setParam (*a, ids::pattern, 3.0f);
    setParam (*a, ids::level (Clap), -12.0f);
    a->patterns.edit (5, [] (Pattern& p)
    {
        p.length = 32;
        auto& s = p.tracks[Tom].steps[17];
        s.on = 1; s.velocity = 77; s.probability = 40; s.micro = -12; s.ratchet = 3; s.tune = -5; s.decay = 30;
        s.setFlag (Step::Accent, true);
    });

    juce::MemoryBlock state;
    a->getStateInformation (state);

    auto b = makeProcessor();
    b->setStateInformation (state.getData(), (int) state.getSize());

    CHECK (std::abs (getParam (*b, ids::synth (Kick, KickP::Tune)) - 61.0f) < 0.01f);
    CHECK (std::abs (getParam (*b, ids::swing) - 63.0f) < 0.01f);
    CHECK ((int) getParam (*b, ids::pattern) == 3);
    CHECK (std::abs (getParam (*b, ids::level (Clap)) + 12.0f) < 0.01f);

    const auto pa = a->patterns.getBank();
    const auto pb = b->patterns.getBank();
    CHECK (std::memcmp (&pa, &pb, sizeof (PatternBank)) == 0);
    const auto& s = pb.patterns[5].tracks[Tom].steps[17];
    CHECK (s.on && s.velocity == 77 && s.probability == 40 && s.micro == -12 && s.ratchet == 3 && s.tune == -5 && s.decay == 30 && s.accent());
}

FP_TEST (legacy_state_migration)
{
    // Version 0 state: a bare parameter tree from early builds.
    {
        auto p = makeProcessor();
        juce::XmlElement params ("PARAMS");
        auto* e = params.createNewChildElement ("PARAM");
        e->setAttribute ("id", ids::synth (Kick, KickP::Tune));
        e->setAttribute ("value", 70.0);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (params, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (std::abs (getParam (*p, ids::synth (Kick, KickP::Tune)) - 70.0f) < 0.01f);
    }

    // Version 1 state with unknown parameters and missing sections.
    {
        auto p = makeProcessor();
        juce::XmlElement root ("FLOORPLAN");
        root.setAttribute ("stateVersion", 1);
        auto* params = root.createNewChildElement ("PARAMS");
        auto* known = params->createNewChildElement ("PARAM");
        known->setAttribute ("id", ids::swing);
        known->setAttribute ("value", 58.0);
        auto* unknown = params->createNewChildElement ("PARAM");
        unknown->setAttribute ("id", "future_param");
        unknown->setAttribute ("value", 1.0);
        auto* samples = root.createNewChildElement ("SAMPLES");
        auto* missing = samples->createNewChildElement ("SAMPLE");
        missing->setAttribute ("voice", "snare");
        missing->setAttribute ("path", "Z:/definitely/missing/file.wav");

        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (root, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (std::abs (getParam (*p, ids::swing) - 58.0f) < 0.01f);
        CHECK (std::abs (getParam (*p, ids::synth (Kick, KickP::Tune)) - kVoiceSpecs[Kick].params[KickP::Tune].def) < 0.01f);
        CHECK (p->isSampleMissing (Snare));        // reported, not crashed
        CHECK (processBlocks (*p, 4) >= 0.0f);     // still renders finite audio
    }

    // Garbage must be ignored without touching the current state.
    {
        auto p = makeProcessor();
        const char junk[] = "not a plugin state";
        p->setStateInformation (junk, (int) sizeof (junk));
        CHECK (std::abs (getParam (*p, ids::swing) - 54.0f) < 0.01f);
    }
}

FP_TEST (pattern_json_round_trip)
{
    Pattern p = factory::patterns()[2].pattern;
    p.tracks[Perc].steps[9].micro = 20;
    p.tracks[Perc].steps[9].on = 1;
    p.tracks[Perc].steps[9].setFlag (Step::Flam, true);
    const auto json = serial::patternToJson (p);
    auto back = serial::patternFromJson (json);
    CHECK (back.has_value());
    if (back)
    {
        CHECK (back->length == p.length);
        for (int v = 0; v < kNumVoices; ++v)
            for (int i = 0; i < kMaxSteps; ++i)
            {
                const auto& x = p.tracks[(size_t) v].steps[(size_t) i];
                const auto& y = back->tracks[(size_t) v].steps[(size_t) i];
                CHECK (x.on == y.on);
                if (x.on)
                    CHECK (std::memcmp (&x, &y, sizeof (Step)) == 0);
            }
    }

    PatternBank bank = factory::defaultBank();
    auto bankBack = serial::bankFromJson (serial::bankToJson (bank));
    CHECK (bankBack.has_value());
    if (bankBack)
        for (int i = 0; i < kNumPatterns; ++i)
            CHECK (bankBack->patterns[(size_t) i].length == bank.patterns[(size_t) i].length);

    // Documented example from docs/PATTERN_FORMAT.md.
    const auto doc = serial::patternFromJson (R"({ "format": "floorplan-pattern", "version": 1, "length": 16,
        "tracks": { "kick": [ { "i": 0 }, { "i": 4 }, { "i": 8 }, { "i": 12, "v": 110, "a": true } ],
                    "chh":  [ { "i": 2, "v": 80, "p": 75, "r": 2 } ] } })");
    CHECK (doc.has_value());
    if (doc)
    {
        CHECK (doc->tracks[Kick].steps[12].accent() && doc->tracks[Kick].steps[12].velocity == 110);
        CHECK (doc->tracks[ClosedHat].steps[2].ratchet == 2 && doc->tracks[ClosedHat].steps[2].probability == 75);
    }
}

FP_TEST (pattern_json_rejects_garbage)
{
    CHECK (! serial::patternFromJson ("").has_value());
    CHECK (! serial::patternFromJson ("{ \"format\": \"something-else\" }").has_value());
    CHECK (! serial::patternFromJson ("{ \"format\": \"floorplan-pattern\", \"version\": 99, \"tracks\": {} }").has_value());
    CHECK (! serial::bankFromJson ("[1,2,3]").has_value());

    // Out-of-range values are clamped, not trusted.
    auto p = serial::patternFromJson (R"({ "format": "floorplan-pattern", "version": 1, "length": 500,
        "tracks": { "kick": [ { "i": 3, "v": 900, "p": -4, "r": 12, "m": 99 }, { "i": 999 } ] } })");
    CHECK (p.has_value());
    if (p)
    {
        const auto& s = p->tracks[Kick].steps[3];
        CHECK (p->length == kMaxSteps);
        CHECK (s.velocity == 127 && s.probability == 0 && s.ratchet == 4 && s.micro == 50);
    }
}

FP_TEST (factory_presets_load)
{
    auto p = makeProcessor();
    const int numFactory = p->presets.getNumFactory();
    CHECK (numFactory >= 6);

    for (int i = 0; i < numFactory; ++i)
    {
        p->presets.load (i);
        CHECK (p->presets.getCurrentIndex() == i);
        for (const auto& [id, value] : factory::kits()[(size_t) i].values)
        {
            CHECK (p->apvts.getParameter (id) != nullptr); // every factory value targets a real parameter
            if (p->apvts.getParameter (id) != nullptr)
                CHECK (std::abs (getParam (*p, id) - value) < 0.05f * juce::jmax (1.0f, std::abs (value)));
        }
        p->setInternalPlaying (true);
        const float peak = processBlocks (*p, 40);
        CHECK (peak > 0.01f && peak <= 1.0f);
    }

    // A kit file with only some parameters: the rest return to defaults.
    const auto file = juce::File::createTempFile (".fpkit");
    file.replaceWithText ("<FloorplanKit version=\"1\"><PARAM id=\"kick_decay\" value=\"999\"/></FloorplanKit>");
    setParam (*p, ids::synth (Snare, SnareP::Tune), 300.0f);
    CHECK (p->presets.loadFromFile (file));
    CHECK (std::abs (getParam (*p, ids::synth (Kick, KickP::Decay)) - 999.0f) < 1.0f);
    CHECK (std::abs (getParam (*p, ids::synth (Snare, SnareP::Tune)) - kVoiceSpecs[Snare].params[SnareP::Tune].def) < 0.5f);
    file.deleteFile();
}

FP_TEST (bus_layouts)
{
    auto p = makeProcessor();
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p->isBusesLayoutSupported (layout));

    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (! p->isBusesLayoutSupported (mono));

    auto multi = layout;
    for (int i = 0; i < 8; ++i)
        multi.outputBuses.add (i % 2 == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    CHECK (p->isBusesLayoutSupported (multi));

    // Route the snare to output 2 with that bus enabled: it must leave the main mix.
    CHECK (p->getBusCount (false) == 9);
    auto* bus = p->getBus (false, 1);
    CHECK (bus != nullptr && bus->enable (true));
    p->prepareToPlay (kRate, kBlock);
    setParam (*p, ids::output (Snare), 1.0f);

    juce::AudioBuffer<float> buffer (p->getTotalNumOutputChannels(), kBlock);
    CHECK (buffer.getNumChannels() == 4);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 120), 0);
    buffer.clear();
    p->processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 0, kBlock) == 0.0f);
    CHECK (buffer.getMagnitude (2, 0, kBlock) > 0.01f);
}

FP_TEST (parameters_are_automatable)
{
    auto p = makeProcessor();
    std::set<juce::String> idsSeen;
    int count = 0;
    for (auto* param : p->getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        CHECK (ranged != nullptr);
        if (ranged == nullptr)
            continue;
        ++count;
        CHECK (ranged->isAutomatable());
        CHECK (idsSeen.insert (ranged->getParameterID()).second);
        CHECK (ranged->getDefaultValue() >= 0.0f && ranged->getDefaultValue() <= 1.0f);

        // Host automation writes normalised values; the raw atomic must follow.
        const float before = p->apvts.getRawParameterValue (ranged->getParameterID())->load();
        ranged->setValueNotifyingHost (1.0f);
        const float atMax = p->apvts.getRawParameterValue (ranged->getParameterID())->load();
        CHECK (std::abs (atMax - ranged->getNormalisableRange().end) < 1.0e-3f);
        ranged->setValueNotifyingHost (ranged->convertTo0to1 (before));
    }
    int expected = 4 + 9 + kChainSlots;
    for (int v = 0; v < kNumVoices; ++v)
        expected += numVoiceParams (v) + 9;
    CHECK (count == expected);
    std::printf ("    %d automatable parameters\n", count);
}

FP_TEST (advanced_parameters_keep_existing_indices)
{
    // The 141 parameters of 0.1.0 keep their positions, then the 14 Advanced
    // ones of 0.1.1 (kick, snare, clap), then the hat, cymbal, perc and tom
    // extras added after it.
    auto p = makeProcessor();
    const auto& params = p->getParameters();
    auto idAt = [&] (int i) { return dynamic_cast<juce::RangedAudioParameter*> (params[i])->getParameterID(); };
    CHECK (idAt (0) == ids::synth (Kick, KickP::Tune));
    CHECK (idAt (140) == ids::midiOut);
    CHECK (idAt (141) == ids::synth (Kick, KickP::Attack));
    CHECK (idAt (146) == ids::synth (Snare, SnareP::Drop));
    CHECK (idAt (151) == ids::synth (Clap, ClapP::Attack));
    CHECK (idAt (154) == ids::synth (Clap, ClapP::Jitter));
    CHECK (idAt (155) == ids::synth (ClosedHat, MetalP::Attack));
    CHECK (idAt (params.size() - 1) == ids::synth (Cymbal, MetalP::Stick));
}

FP_TEST (old_state_sets_new_parameters_to_default)
{
    // A state saved before the kick's Advanced parameters existed must load
    // them at their defaults, even over a processor where they were changed.
    auto p = makeProcessor();
    setParam (*p, ids::synth (Kick, KickP::Attack), 12.0f);
    setParam (*p, ids::synth (Kick, KickP::Sub), 80.0f);

    juce::XmlElement root ("FLOORPLAN");
    root.setAttribute ("stateVersion", 1);
    auto* params = root.createNewChildElement ("PARAMS");
    auto* tune = params->createNewChildElement ("PARAM");
    tune->setAttribute ("id", ids::synth (Kick, KickP::Tune));
    tune->setAttribute ("value", 55.0);
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (root, mb);
    p->setStateInformation (mb.getData(), (int) mb.getSize());

    CHECK (std::abs (getParam (*p, ids::synth (Kick, KickP::Tune)) - 55.0f) < 0.01f);
    CHECK (getParam (*p, ids::synth (Kick, KickP::Attack)) == 0.0f);
    CHECK (getParam (*p, ids::synth (Kick, KickP::Sub)) == 0.0f);
    CHECK (std::abs (getParam (*p, ids::synth (Kick, KickP::ClickTone)) - 1800.0f) < 1.0f);
    CHECK (std::abs (getParam (*p, ids::synth (OpenHat, MetalP::HighCut)) - 20000.0f) < 1.0f);
    CHECK (std::abs (getParam (*p, ids::synth (Tom, ToneP::NoiseLength)) - 30.0f) < 0.01f);

    // And a current state round-trips them.
    setParam (*p, ids::synth (Kick, KickP::Shape), 40.0f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (getParam (*q, ids::synth (Kick, KickP::Shape)) - 40.0f) < 0.01f);
}

// Renders the real editor offscreen to a PNG (used for docs screenshots and
// as a smoke test that the whole UI can be constructed and painted).
static void clickButtonNamed (juce::Component& root, const juce::String& text)
{
    for (auto* child : root.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getButtonText() == text)
        {
            b->setToggleState (true, juce::dontSendNotification);
            if (b->onClick)
                b->onClick();
            return;
        }
        clickButtonNamed (*child, text);
    }
}

// Optional `tab`: the text of an Advanced Mode tab to show (e.g. "PITCH/ENV");
// `voice`: the voice to select (default: the kick).
static int renderScreenshot (const juce::File& target, float scale, bool advanced, const juce::String& tab = {}, int voice = 0)
{
    auto p = makeProcessor();
    p->setInternalPlaying (true);
    processBlocks (*p, 40);
    p->apvts.state.setProperty ("uiAdvancedMode", advanced, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    editor->setSize (1280, 800);
    for (auto* child : editor->getChildren())
        if (auto* main = dynamic_cast<ui::MainComponent*> (child))
            main->selectVoice (voice);
    if (tab.isNotEmpty())
        clickButtonNamed (*editor, tab);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    target.deleteFile();
    juce::FileOutputStream out (target);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s (%dx%d)\n", ok ? "Wrote" : "FAILED to write", target.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return ok ? 0 : 1;
}

// Renders the app icon (Resources/icon*.png): the header's stacked "floor"
// bars on a cream key with an ink outline. Drawn on a 32-unit grid so the
// bars sit on whole pixels at 32 px and on half-pixel steps at 16 px.
static int renderIcon (const juce::File& target, int size)
{
    juce::Image image (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (image);
        const float u = (float) size / 32.0f;
        const auto key = juce::Rectangle<float> (0.0f, 0.0f, 32.0f * u, 32.0f * u).reduced (0.5f * u);
        g.setColour (ui::colours::background);
        g.fillRoundedRectangle (key, 6.5f * u);
        g.setColour (ui::colours::ink);
        g.drawRoundedRectangle (key, 6.5f * u, u);

        // Four bars, 4 units tall with 2-unit gaps, widening by 3 units; the
        // bottom one orange, as in the header wordmark.
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i == 3 ? ui::colours::accent : ui::colours::ink);
            g.fillRect (7.0f * u, (5.0f + 6.0f * (float) i) * u, (9.0f + 3.0f * (float) i) * u, 4.0f * u);
        }
    }

    target.deleteFile();
    juce::FileOutputStream out (target);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s (%dx%d)\n", ok ? "Wrote" : "FAILED to write", target.getFullPathName().toRawUTF8(), size, size);
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc > 3 && std::strcmp (argv[1], "--icon") == 0)
        return renderIcon (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), juce::jlimit (16, 2048, std::atoi (argv[3])));
    if (argc > 2 && std::strcmp (argv[1], "--screenshot") == 0)
        return renderScreenshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                                 argc > 3 ? (float) std::atof (argv[3]) : 1.0f,
                                 argc > 4 && std::strcmp (argv[4], "--advanced") == 0,
                                 argc > 5 ? juce::String (argv[5]) : juce::String(),
                                 argc > 6 ? juce::jlimit (0, kNumVoices - 1, std::atoi (argv[6])) : 0);
    return fptest::runMain (argc, argv);
}
