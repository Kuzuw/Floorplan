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

    // Routing, the sequencer switch and a learned MIDI note travel too.
    setParam (*a, ids::output (Snare), 2.0f);
    setParam (*a, ids::sequencerOn, 0.0f);
    a->beginNoteLearn (Clap);
    juce::MidiBuffer learn;
    learn.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    processBlocks (*a, 1, &learn);
    CHECK (a->getVoiceNote (Clap) == 64);

    juce::MemoryBlock state2;
    a->getStateInformation (state2);
    auto c = makeProcessor();
    c->setStateInformation (state2.getData(), (int) state2.getSize());
    CHECK ((int) getParam (*c, ids::output (Snare)) == 2);
    CHECK (getParam (*c, ids::sequencerOn) < 0.5f);
    CHECK (c->getVoiceNote (Clap) == 64);
    CHECK (c->getVoiceNote (Kick) == kVoiceSpecs[Kick].defaultMidiNote);
}

FP_TEST (midi_note_map_and_velocity)
{
    // Each default note plays its voice and only that voice.
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto p = makeProcessor();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, kVoiceSpecs[(size_t) v].defaultMidiNote, (juce::uint8) 100), 3);
        CHECK (processBlocks (*p, 2, &midi) > 1.0e-3f);
        for (int w = 0; w < kNumVoices; ++w)
            CHECK (p->getTriggerCount (w) == (w == v ? 1u : 0u));
    }

    // Velocity scales the hit.
    auto peakAt = [] (juce::uint8 velocity)
    {
        auto p = makeProcessor();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 38, velocity), 0);
        return processBlocks (*p, 10, &midi);
    };
    const float loud = peakAt (127), soft = peakAt (30);
    CHECK (soft > 0.0f && loud > soft * 3.0f);

    auto p = makeProcessor();
    // Note-offs and velocity-0 note-ons (a note-off by another name) trigger nothing.
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOff (10, 36), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 0), 5);
        CHECK (processBlocks (*p, 2, &midi) == 0.0f);
        CHECK (p->getTriggerCount (Kick) == 0 && p->getTriggerCount (Snare) == 0);
    }
    // Overlapping notes: two kicks in one block retrigger, kick and snare together both play.
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 300);
        processBlocks (*p, 2, &midi);
        CHECK (p->getTriggerCount (Kick) == 2 && p->getTriggerCount (Snare) == 1);
    }
    // Learning a note remaps the voice; the old note stops playing it.
    {
        p->beginNoteLearn (Snare);
        juce::MidiBuffer learn;
        learn.addEvent (juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100), 0);
        processBlocks (*p, 1, &learn);
        CHECK (p->getVoiceNote (Snare) == 60);
        CHECK (p->getTriggerCount (Snare) == 2); // the learning note also plays

        juce::MidiBuffer old;
        old.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);
        processBlocks (*p, 1, &old);
        CHECK (p->getTriggerCount (Snare) == 2);

        p->resetVoiceNotes();
        CHECK (p->getVoiceNote (Snare) == 38);
    }
}

FP_TEST (all_sound_off_silences_and_all_notes_off_does_not)
{
    auto ringing = [] (const juce::MidiMessage& panic)
    {
        auto p = makeProcessor();
        juce::MidiBuffer hit;
        hit.addEvent (juce::MidiMessage::noteOn (10, 49, (juce::uint8) 127), 0); // cymbal, 1.6 s decay
        processBlocks (*p, 5, &hit);
        juce::MidiBuffer stop;
        stop.addEvent (panic, 0);
        processBlocks (*p, 4, &stop); // the 4 ms fade runs out within ~40 ms
        return processBlocks (*p, 3);
    };
    CHECK (ringing (juce::MidiMessage::allSoundOff (10)) == 0.0f);
    CHECK (ringing (juce::MidiMessage::allNotesOff (10)) > 1.0e-3f);
}

FP_TEST (host_play_ends_the_preview)
{
    auto p = makeProcessor();
    FakePlayHead head;
    head.playing = false;
    head.bpm = 60.0;
    p->setPlayHead (&head);

    // Preview while the host is stopped runs at the host's tempo: two kicks in two seconds at 60 BPM.
    p->setInternalPlaying (true);
    processBlocks (*p, (int) (kRate * 2.0 / kBlock), nullptr, &head);
    CHECK (p->getTriggerCount (Kick) == 2);

    // The host starts: preview ends and the host drives the pattern.
    head.playing = true;
    head.ppq = 0.0;
    processBlocks (*p, 1, nullptr, &head);
    CHECK (! p->isInternalPlaying());
    CHECK (p->getTriggerCount (Kick) == 3);

    // The host stops: nothing keeps playing.
    head.playing = false;
    const auto before = p->getTriggerCount (Kick);
    processBlocks (*p, 200, nullptr, &head);
    CHECK (p->getTriggerCount (Kick) == before);
    CHECK (p->getPlayingStep() == -1);
    p->setPlayHead (nullptr);
}

namespace
{
struct OddPlayHead final : juce::AudioPlayHead
{
    int kind = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        switch (kind)
        {
            case 0: info.setBpm (std::numeric_limits<double>::quiet_NaN()); info.setPpqPosition (std::numeric_limits<double>::infinity()); break;
            case 1: info.setBpm (0.0); break;                 // no position at all
            case 2: info.setBpm (-50.0); info.setPpqPosition (std::numeric_limits<double>::quiet_NaN()); break;
            default: return {};                               // no information
        }
        return info;
    }
};
} // namespace

FP_TEST (invalid_host_timing_is_safe)
{
    // Nonsense tempo or position from a host: the plugin keeps running on its
    // fallback clock, in time, with finite audio.
    for (int kind = 0; kind < 4; ++kind)
    {
        auto p = makeProcessor();
        OddPlayHead head;
        head.kind = kind;
        p->setPlayHead (&head);
        if (kind == 3)
            p->setInternalPlaying (true); // no host information: only the preview plays
        CHECK (processBlocks (*p, 200) >= 0.0f);
        CHECK (std::isfinite (p->getDisplayBpm()) && p->getDisplayBpm() > 0.0);
        CHECK (p->getTriggerCount (Kick) >= 2);
        p->setPlayHead (nullptr);
    }

    // A count-in reports positions before zero: the grid still lines up. From
    // beat -1, a kick plays at -1 (step 12 of the previous bar) and at 0.
    auto p = makeProcessor();
    FakePlayHead head;
    head.ppq = -1.0;
    p->setPlayHead (&head);
    processBlocks (*p, (int) (kRate * 0.6 / kBlock), nullptr, &head);
    CHECK (p->getTriggerCount (Kick) == 2);
    p->setPlayHead (nullptr);
}

FP_TEST (sequencer_can_be_switched_off)
{
    auto p = makeProcessor();
    setParam (*p, ids::sequencerOn, 0.0f);
    p->setInternalPlaying (true);
    CHECK (processBlocks (*p, 100) == 0.0f);
    CHECK (p->getTriggerCount (Kick) == 0);
    CHECK (! p->isTransportRunning());

    // MIDI input still plays.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 0);
    processBlocks (*p, 2, &midi);
    CHECK (p->getTriggerCount (Kick) == 1);

    setParam (*p, ids::sequencerOn, 1.0f);
    processBlocks (*p, 100);
    CHECK (p->getTriggerCount (Kick) >= 3);
}

FP_TEST (midi_and_sequencer_do_not_double)
{
    // The host plays the pattern and a MIDI clip with the same kick on beat 1
    // (as when the sequencer's MIDI output was recorded and played back).
    auto p = makeProcessor();
    FakePlayHead head;
    p->setPlayHead (&head);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 40), 0);
    processBlocks (*p, 1, &midi, &head);
    CHECK (p->getTriggerCount (Kick) == 1);

    // A MIDI kick clearly apart from the pattern's (10 ms later) is its own hit.
    auto q = makeProcessor();
    FakePlayHead head2;
    q->setPlayHead (&head2);
    juce::MidiBuffer late;
    late.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 40), (int) (0.010 * kRate));
    processBlocks (*q, 1, &late, &head2);
    CHECK (q->getTriggerCount (Kick) == 2);

    p->setPlayHead (nullptr);
    q->setPlayHead (nullptr);
}

FP_TEST (automation_fast_and_slow_reaches_the_engine)
{
    // A long, low kick gives a smooth signal, so any jump in gain shows up as a
    // jump between neighbouring samples.
    auto p = makeProcessor();
    setParam (*p, ids::synth (Kick, KickP::Decay), 2500.0f);
    setParam (*p, ids::synth (Kick, KickP::Click), 0.0f);
    setParam (*p, ids::masterClip, 0.0f);

    juce::AudioBuffer<float> buffer (p->getTotalNumOutputChannels(), kBlock);
    juce::MidiBuffer hit;
    hit.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 127), 0);
    auto run = [&] (juce::MidiBuffer& midi, float& maxStep, float& prev)
    {
        buffer.clear();
        p->processBlock (buffer, midi);
        midi.clear();
        const float* l = buffer.getReadPointer (0);
        for (int i = 0; i < kBlock; ++i)
        {
            maxStep = juce::jmax (maxStep, std::abs (l[i] - prev));
            prev = l[i];
        }
        return buffer.getRMSLevel (0, 0, kBlock);
    };

    float maxStep = 0.0f, prev = 0.0f;
    for (int b = 0; b < 10; ++b) // past the pitch sweep (run() clears the MIDI after the first block)
        run (hit, maxStep, prev);

    // Fast: the level jumps 30 dB every four blocks (about 43 ms).
    maxStep = 0.0f;
    float loudRms = 0.0f, quietRms = 1.0f;
    for (int b = 0; b < 32; ++b)
    {
        const bool loud = (b / 4) % 2 == 0;
        setParam (*p, ids::level (Kick), loud ? 0.0f : -30.0f);
        juce::MidiBuffer none;
        const float rms = run (none, maxStep, prev);
        if (b % 4 == 3) // last block of a segment: settled
            (loud ? loudRms : quietRms) = rms;
    }
    CHECK (loudRms > quietRms * 10.0f); // the automation reached the sound
    CHECK (maxStep < 0.03f);            // and was smoothed: no clicks

    // Slow: a one-second fade up from -60 dB rises steadily.
    juce::MidiBuffer again;
    again.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 127), 0);
    std::vector<float> rms;
    maxStep = 0.0f;
    const int fadeBlocks = (int) (kRate / kBlock);
    for (int b = 0; b < fadeBlocks; ++b)
    {
        setParam (*p, ids::level (Kick), -60.0f + 60.0f * (float) b / (float) (fadeBlocks - 1));
        rms.push_back (run (again, maxStep, prev));
    }
    CHECK (rms.back() > rms[(size_t) fadeBlocks / 2] && rms[(size_t) fadeBlocks / 2] > rms[5]);
    CHECK (maxStep < 0.03f);
}

FP_TEST (missing_sample_is_found_by_name)
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("floorplan_sample_search");
    root.deleteRecursively();
    const auto nested = root.getChildFile ("Packs").getChildFile ("909");
    CHECK (nested.createDirectory());
    const auto wav = nested.getChildFile ("MySnare.wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wav);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                          .withSampleRate (kRate)
                                                                          .withNumChannels (1)
                                                                          .withBitsPerSample (16));
        CHECK (writer != nullptr);
        juce::AudioBuffer<float> tone (1, 4800);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.5f * std::sin (0.05f * (float) i));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    }

    juce::XmlElement state ("FLOORPLAN");
    state.setAttribute ("stateVersion", 1);
    auto* sample = state.createNewChildElement ("SAMPLES")->createNewChildElement ("SAMPLE");
    sample->setAttribute ("voice", "snare");
    sample->setAttribute ("path", "Z:\\Old Machine\\Samples\\MySnare.wav");
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (state, mb);

    // Found by name anywhere under the search folder.
    auto p = makeProcessor();
    p->setSampleSearchFolder (root);
    p->setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (! p->isSampleMissing (Snare));
    CHECK (juce::File (p->getSamplePath (Snare)) == wav);

    // Not found: reported as missing, the saved path kept, synthesis plays.
    auto q = makeProcessor();
    q->setSampleSearchFolder (root.getChildFile ("empty"));
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (q->isSampleMissing (Snare));
    CHECK (q->getSamplePath (Snare) == "Z:\\Old Machine\\Samples\\MySnare.wav");
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 0);
    CHECK (processBlocks (*q, 4, &midi) > 1.0e-3f);

    root.deleteRecursively();
}

FP_TEST (any_block_size_and_sample_rate_keeps_time)
{
    // Hosts change block sizes from call to call and may exceed the size they
    // announced. One bar at 120 BPM must still give exactly four kicks.
    auto p = std::make_unique<FloorplanProcessor>();
    p->setRateAndBufferSizeDetails (44100.0, 256);
    p->prepareToPlay (44100.0, 256);
    FakePlayHead head;
    p->setPlayHead (&head);

    const int sizes[] = { 1, 7, 256, 1000, 4096, 64, 333 };
    const int total = (int) (44100.0 * 2.0) - 200;
    juce::AudioBuffer<float> buffer (p->getTotalNumOutputChannels(), 4096);
    bool finite = true;
    int done = 0;
    for (int i = 0; done < total; ++i)
    {
        const int n = juce::jmin (sizes[i % 7], total - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
        view.clear();
        juce::MidiBuffer midi;
        p->processBlock (view, midi);
        for (int ch = 0; ch < view.getNumChannels(); ++ch)
            finite = finite && std::isfinite (view.getMagnitude (ch, 0, n));
        head.ppq += n * head.bpm / (60.0 * 44100.0);
        done += n;
    }
    CHECK (finite);
    CHECK (p->getTriggerCount (Kick) == 4);

    // A sample-rate change mid-session (the host re-prepares): still in time.
    p->setRateAndBufferSizeDetails (96000.0, 64);
    p->prepareToPlay (96000.0, 64);
    head.ppq = 0.0;
    const auto before = p->getTriggerCount (Kick);
    juce::AudioBuffer<float> hi (p->getTotalNumOutputChannels(), 64);
    for (int b = 0; b < (int) (96000.0 * 2.0 / 64) - 4; ++b)
    {
        hi.clear();
        juce::MidiBuffer midi;
        p->processBlock (hi, midi);
        head.ppq += 64 * head.bpm / (60.0 * 96000.0);
    }
    CHECK (p->getTriggerCount (Kick) - before == 4);
    p->setPlayHead (nullptr);
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

    // The eight extra outputs are active by default (silent until used).
    CHECK (p->getBusCount (false) == 9);
    CHECK (p->getTotalNumOutputChannels() == 18);
}

FP_TEST (multi_output_routing_in_every_layout)
{
    // Three layouts a host can choose: every output, main only, and every
    // other output disabled. For each voice routed to "its" output (Out v+2):
    // if that bus is enabled the voice is there and nowhere else, otherwise it
    // falls back to the main mix. Either way it is heard exactly once.
    for (int layoutKind = 0; layoutKind < 3; ++layoutKind)
    {
        auto p = std::make_unique<FloorplanProcessor>();
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        for (int b = 0; b < kNumVoices; ++b)
        {
            const bool on = layoutKind == 0 || (layoutKind == 2 && b % 2 == 0);
            layout.outputBuses.add (on ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        }
        CHECK (p->setBusesLayout (layout));
        p->setRateAndBufferSizeDetails (kRate, kBlock);
        p->prepareToPlay (kRate, kBlock);
        for (int v = 0; v < kNumVoices; ++v)
            setParam (*p, ids::output (v), (float) (v + 1));

        for (int v = 0; v < kNumVoices; ++v)
        {
            juce::AudioBuffer<float> buffer (p->getTotalNumOutputChannels(), kBlock);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, kVoiceSpecs[(size_t) v].defaultMidiNote, (juce::uint8) 120), 0);
            buffer.clear();
            p->processBlock (buffer, midi);

            const bool ownBus = p->getBus (false, v + 1)->isEnabled();
            for (int b = 0; b <= kNumVoices; ++b)
            {
                auto bus = p->getBusBuffer (buffer, false, b);
                const float level = bus.getNumChannels() > 0 ? bus.getMagnitude (0, kBlock) : 0.0f;
                const bool expected = ownBus ? b == v + 1 : b == 0;
                CHECK (expected ? level > 1.0e-3f : level == 0.0f);
            }

            // Let the voice ring out before the next one.
            for (int i = 0; i < 400; ++i)
            {
                juce::MidiBuffer none;
                p->processBlock (buffer, none);
            }
        }
    }
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
    int expected = 4 + 9 + kChainSlots + 1 + 5; // master, sequencer, chain slots, Sequencer On, Character
    for (int v = 0; v < kNumVoices; ++v)
        expected += numVoiceParams (v) + 9 + 1; // + Choked By
    CHECK (count == expected);
    std::printf ("    %d automatable parameters\n", count);
}

FP_TEST (advanced_parameters_keep_existing_indices)
{
    // The 141 parameters of 0.1.0 keep their positions, then the 14 Advanced
    // ones of 0.1.1 (kick, snare, clap), then the hat, cymbal, perc and tom
    // extras added after it, Sequencer On, the Character section and the
    // Choked By selectors.
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
    CHECK (idAt (184) == ids::synth (Cymbal, MetalP::Stick));
    CHECK (idAt (185) == ids::sequencerOn);
    CHECK (idAt (186) == ids::charOn);
    CHECK (idAt (190) == ids::charVelTone);
    CHECK (idAt (191) == ids::choke (Kick));
    CHECK (idAt (198) == ids::choke (Cymbal));
    CHECK (params.size() == 199);
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
    // Older projects sound as they did: Character off, the open hat still
    // choked by the closed hat, no other chokes.
    CHECK (getParam (*p, ids::charOn) == 0.0f);
    CHECK ((int) getParam (*p, ids::choke (OpenHat)) == ClosedHat + 1);
    CHECK ((int) getParam (*p, ids::choke (Kick)) == 0);

    // And a current state round-trips them.
    setParam (*p, ids::synth (Kick, KickP::Shape), 40.0f);
    setParam (*p, ids::charOn, 1.0f);
    setParam (*p, ids::charPunch, -35.0f);
    setParam (*p, ids::choke (Cymbal), (float) (Kick + 1));
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (getParam (*q, ids::synth (Kick, KickP::Shape)) - 40.0f) < 0.01f);
    CHECK (getParam (*q, ids::charOn) == 1.0f);
    CHECK (std::abs (getParam (*q, ids::charPunch) + 35.0f) < 0.01f);
    CHECK ((int) getParam (*q, ids::choke (Cymbal)) == Kick + 1);
}

FP_TEST (character_and_choke_controls_change_the_sound)
{
    // Every new control must change the output, and Character off must leave
    // it bit-identical whatever the section's settings.
    auto render = [] (const std::function<void (FloorplanProcessor&)>& setup)
    {
        auto p = makeProcessor();
        setup (*p);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (p->getTotalNumOutputChannels(), kBlock);
        for (int b = 0; b < 30; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 1)
            {
                midi.addEvent (juce::MidiMessage::noteOn (10, p->getVoiceNote (Snare), (juce::uint8) 70), 10);
                midi.addEvent (juce::MidiMessage::noteOn (10, p->getVoiceNote (OpenHat), (juce::uint8) 90), 20);
                midi.addEvent (juce::MidiMessage::noteOn (10, p->getVoiceNote (Kick), (juce::uint8) 110), 30);
            }
            if (b == 6)
                midi.addEvent (juce::MidiMessage::noteOn (10, p->getVoiceNote (ClosedHat), (juce::uint8) 60), 0);
            buffer.clear();
            p->processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
        }
        return out;
    };
    auto withCharacter = [] (FloorplanProcessor& p) { setParam (p, ids::charOn, 1.0f); };

    const auto plain = render ([] (FloorplanProcessor&) {});
    const auto offButSet = render ([] (FloorplanProcessor& p)
    {
        setParam (p, ids::charColor, 100.0f);
        setParam (p, ids::charPunch, 80.0f);
        setParam (p, ids::charVary, 100.0f);
        setParam (p, ids::charVelTone, 100.0f);
    });
    CHECK (offButSet == plain);

    const auto on = render (withCharacter);
    CHECK (on != plain);
    struct Change { juce::String id; float value; };
    for (auto change : { Change { ids::charColor, 90.0f }, Change { ids::charPunch, 60.0f }, Change { ids::charPunch, -60.0f },
                         Change { ids::charVary, 100.0f }, Change { ids::charVelTone, 0.0f }, Change { ids::seed, 2.0f } })
    {
        const auto changed = render ([&] (FloorplanProcessor& p) { withCharacter (p); setParam (p, change.id, change.value); });
        if (changed == on)
            std::printf ("    %s = %g did not change the sound\n", change.id.toRawUTF8(), (double) change.value);
        CHECK (changed != on);
    }

    // Choked By: None lets the open hat ring past the closed hat.
    const auto unchoked = render ([] (FloorplanProcessor& p) { setParam (p, ids::choke (OpenHat), 0.0f); });
    CHECK (unchoked != plain);
}

FP_TEST (embedded_fonts_load)
{
    // A font the platform rejects would silently fall back to a system font.
    for (auto face : { ui::Face::body, ui::Face::bodyMedium, ui::Face::bodySemiBold, ui::Face::headline, ui::Face::headlineBold })
        CHECK (ui::typeface (face) != nullptr);
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

// Renders four bars at 124 BPM through the processor with a host transport:
// the default pattern plus one MIDI hit per voice at different velocities in
// bar 2, so every voice is heard. Writes the main output as a 32-bit float
// WAV. Extra arguments `id=value` set parameters first (real values), e.g.
// char_on=1. Used for before/after listening and bit-exact comparisons.
static int renderDemo (const juce::File& target, int argc, char** argv, int firstParam)
{
    auto p = makeProcessor();
    for (int i = firstParam; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        auto* param = p->apvts.getParameter (arg.upToFirstOccurrenceOf ("=", false, false));
        if (param == nullptr)
        {
            std::printf ("Unknown parameter in %s\n", argv[i]);
            return 1;
        }
        param->setValueNotifyingHost (param->convertTo0to1 (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    FakePlayHead head;
    head.bpm = 124.0;
    p->setPlayHead (&head);

    const int total = (int) (kRate * 4.0 * 4.0 * 60.0 / head.bpm);
    const int barSamples = total / 4;
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> block (p->getTotalNumOutputChannels(), kBlock);
    for (int done = 0; done < total; done += kBlock)
    {
        const int n = juce::jmin (kBlock, total - done);
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), block.getNumChannels(), n);
        view.clear();
        juce::MidiBuffer midi;
        for (int v = 0; v < kNumVoices; ++v)
        {
            const int at = barSamples + v * barSamples / kNumVoices + 1000; // off the pattern's grid
            if (at >= done && at < done + n)
                midi.addEvent (juce::MidiMessage::noteOn (10, kVoiceSpecs[(size_t) v].defaultMidiNote,
                                                          (juce::uint8) (30 + 13 * v)), at - done);
        }
        p->processBlock (view, midi);
        out.copyFrom (0, done, view, 0, 0, n);
        out.copyFrom (1, done, view, 1, 0, n);
        head.ppq += n * head.bpm / (60.0 * kRate);
    }
    p->setPlayHead (nullptr);

    target.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (target);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                      .withSampleRate (kRate)
                                                                      .withNumChannels (2)
                                                                      .withBitsPerSample (32)
                                                                      .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    const bool ok = writer != nullptr && writer->writeFromAudioSampleBuffer (out, 0, total);
    writer.reset();
    std::printf ("%s %s (%.2f s, peak %.3f)\n", ok ? "Wrote" : "FAILED to write", target.getFullPathName().toRawUTF8(),
                 (double) total / kRate, (double) out.getMagnitude (0, total));
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc > 2 && std::strcmp (argv[1], "--render") == 0)
        return renderDemo (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc, argv, 3);
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
