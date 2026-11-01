#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterIDs.h"
#include "Presets/PatternSerializer.h"

namespace fp
{

juce::AudioProcessor::BusesProperties FloorplanProcessor::makeBuses()
{
    auto buses = BusesProperties()
                     .withOutput ("Main", juce::AudioChannelSet::stereo(), true);

    // Per-voice outputs for multi-out routing in a DAW. They are active by
    // default, so hosts that only offer default-active buses (or never activate
    // any) still list them. Every voice plays through Main until its Output
    // parameter picks one of these, so they are silent unless used. The shared
    // code is compiled once for all formats, so check the wrapper at runtime.
    if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_Standalone)
        for (int v = 0; v < kNumVoices; ++v)
            buses = buses.withOutput (juce::String ("Out ") + juce::String (v + 2) + " (" + kVoiceSpecs[(size_t) v].name + ")",
                                      juce::AudioChannelSet::stereo(), true);
    return buses;
}

namespace
{
constexpr int kMidiOutChannel = 10;
constexpr double kMidiNoteLength = 0.05; // seconds

// A state saved before a parameter existed has no entry for it, and APVTS
// would keep that parameter's current value. Add the default instead, so an
// old project sounds the same whatever was loaded before it.
juce::ValueTree withMissingParametersAtDefault (juce::ValueTree tree, const juce::AudioProcessor& processor)
{
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! tree.getChildWithProperty ("id", rp->getParameterID()).isValid())
            {
                juce::ValueTree child ("PARAM");
                child.setProperty ("id", rp->getParameterID(), nullptr);
                child.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                tree.appendChild (child, nullptr);
            }
    return tree;
}
} // namespace

FloorplanProcessor::FloorplanProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    paramList = getParameters();
    resetVoiceNotes();
    for (auto& c : ccToParam) c.store (-1);
    startTimerHz (20);
}

FloorplanProcessor::~FloorplanProcessor()
{
    stopTimer();
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void FloorplanProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate, block);
    sequencer.prepare (sampleRate);
    loadMeasurer.reset (sampleRate, block);
    monoFallback.setSize (1, block);
    pendingNoteOff.fill ({});
    hostWasPlaying = false;
}

bool FloorplanProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty())
        for (const auto& in : layouts.inputBuses)
            if (! in.isDisabled())
                return false;

    if (layouts.outputBuses.isEmpty() || layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& bus = layouts.outputBuses[i];
        if (! bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

TransportState FloorplanProcessor::readTransport (int numSamples)
{
    TransportState t;
    bool hostPlaying = false;
    std::optional<double> hostPpq;
    double bpm = lastHostBpm;

    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            // Values a host cannot mean (no tempo, NaN) count as missing.
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm(); b && std::isfinite (*b) && *b >= 1.0) bpm = *b;
            if (auto p = pos->getPpqPosition(); p && std::isfinite (*p)) hostPpq = *p;
        }
    }

    // The host's play button ends a preview started from the editor, so the
    // internal clock never runs alongside the host or resumes when it stops.
    if (hostPlaying && ! hostWasPlaying)
        internalPlay.store (false, std::memory_order_relaxed);
    hostWasPlaying = hostPlaying;

    if (hostPlaying)
    {
        lastHostBpm = bpm;
        t.playing = true;
        t.bpm = bpm;
        // Hosts that report "playing" without a musical position get a
        // sample-counted fallback so the groove still runs.
        t.ppq = hostPpq ? *hostPpq : internalPpq;
        internalPpq = t.ppq + numSamples * bpm / (60.0 * getSampleRate());
        uiHostSync.store (true, std::memory_order_relaxed);
    }
    else if (internalPlay.load (std::memory_order_relaxed))
    {
        // Preview: in a host it keeps the host's tempo, so the pattern sounds
        // the same when the host starts. The Standalone app uses its own tempo.
        t.playing = true;
        t.bpm = getPlayHead() != nullptr ? bpm : params.tempo();
        t.ppq = internalPpq;
        internalPpq += numSamples * t.bpm / (60.0 * getSampleRate());
        uiHostSync.store (false, std::memory_order_relaxed);
    }
    else
    {
        internalPpq = 0.0;
        t.bpm = getPlayHead() != nullptr ? bpm : params.tempo();
        uiHostSync.store (getPlayHead() != nullptr, std::memory_order_relaxed);
    }

    uiBpm.store (t.bpm, std::memory_order_relaxed);
    return t;
}

void FloorplanProcessor::pushUiMessage (const MidiUiMessage& m) noexcept
{
    const auto scope = uiFifo.write (1);
    if (scope.blockSize1 > 0)
        uiQueue[(size_t) scope.startIndex1] = m;
    // If the FIFO is full the message is dropped: never block the audio thread.
}

void FloorplanProcessor::handleMidiInput (const juce::MidiBuffer& midi, const TransportState& transport, int numSamples, int& count)
{
    // While the transport runs, a note's hit id comes from its song position
    // (in 1/3840 beat), so the Character variation of a MIDI part is the same
    // on every playback and in an export. Otherwise the engine numbers hits.
    const double ppqPerSample = transport.bpm / (60.0 * getSampleRate());
    auto hitIdAt = [&] (int offset) -> uint32_t
    {
        if (! transport.playing)
            return 0;
        auto x = (uint64_t) std::llround ((transport.ppq + offset * ppqPerSample) * 3840.0) * 0x9E3779B97F4A7C15ull;
        x ^= x >> 29;
        return (uint32_t) (x ^ (x >> 32)) | 1u;
    };

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            const int learnVoice = learnNoteVoice.exchange (-1);
            if (learnVoice >= 0)
            {
                voiceNotes[(size_t) learnVoice].store (note);
                pushUiMessage ({ NoteLearned, learnVoice, note, 0.0f });
            }

            for (int v = 0; v < kNumVoices && count < (int) eventScratch.size(); ++v)
            {
                if (voiceNotes[(size_t) v].load (std::memory_order_relaxed) != note)
                    continue;
                auto& e = eventScratch[(size_t) count++];
                e = TriggerEvent {};
                e.offset = offset;
                e.voice = (uint8_t) v;
                e.velocity = msg.getFloatVelocity();
                e.hitId = hitIdAt (offset);
            }
        }
        else if (msg.isAllSoundOff())
        {
            engine.chokeAll();
        }
        else if (msg.isController() && msg.getControllerNumber() < 120)
        {
            // Channel mode messages (120-127) are never learned or mapped. All
            // Notes Off needs no action: every voice is a one-shot that ignores
            // note-offs, so no note can hang.
            const int cc = msg.getControllerNumber();
            const int learn = learnParam.exchange (-1);
            if (learn >= 0)
            {
                pushUiMessage ({ CcLearned, cc, learn, 0.0f });
                continue;
            }

            const int target = ccToParam[(size_t) cc].load (std::memory_order_relaxed);
            if (target >= 0)
                pushUiMessage ({ CcValue, cc, target, (float) msg.getControllerValue() / 127.0f });
        }
    }
}

void FloorplanProcessor::writeMidiOutput (juce::MidiBuffer& midi, const TriggerEvent* seqEvents, int numSeqEvents, int numSamples)
{
    midi.clear(); // keeps its allocated storage

    // Note-offs scheduled by earlier blocks.
    for (auto& pending : pendingNoteOff)
    {
        if (pending.samplesLeft < 0)
            continue;
        if (pending.samplesLeft < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, pending.note), pending.samplesLeft);
            pending.samplesLeft = -1;
        }
        else
        {
            pending.samplesLeft -= numSamples;
        }
    }

    if (! params.midiOutEnabled())
        return;

    const int noteLength = juce::jmax (1, (int) (kMidiNoteLength * getSampleRate()));
    for (int i = 0; i < numSeqEvents; ++i)
    {
        const auto& e = seqEvents[i];
        const int note = voiceNotes[(size_t) e.voice].load();
        auto& pending = pendingNoteOff[(size_t) e.voice];
        if (pending.samplesLeft >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, pending.note), e.offset);

        const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (e.velocity * 127.0f) + (e.accent ? 10 : 0));
        midi.addEvent (juce::MidiMessage::noteOn (kMidiOutChannel, note, vel), e.offset);

        const int offAt = e.offset + noteLength;
        if (offAt < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, note), offAt);
            pending.samplesLeft = -1;
        }
        else
        {
            pending = { offAt - numSamples, note };
        }
    }
}

void FloorplanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    if (numSamples <= 0)
        return;

    params.readEngine (engine);
    params.readSequencer (seqSettings);

    for (int v = 0; v < kNumVoices; ++v)
        engine.setSample (v, sampleSlots.acquire (v));

    const TransportState transport = readTransport (numSamples);
    int count = 0;

    // 1. Live MIDI input.
    handleMidiInput (midi, transport, numSamples, count);

    // 2. Pads clicked / keys pressed in the editor (always at block start).
    {
        const auto scope = triggerFifo.read (triggerFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1 && count < (int) eventScratch.size(); ++i)
            eventScratch[(size_t) count++] = triggerQueue[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2 && count < (int) eventScratch.size(); ++i)
            eventScratch[(size_t) count++] = triggerQueue[(size_t) (scope.startIndex2 + i)];
    }

    // 3. Sequencer (unless switched off: then only MIDI and the pads play).
    TransportState seqTransport = transport;
    seqTransport.playing = transport.playing && params.sequencerEnabled();
    const PatternBank& bank = patterns.acquireForAudio();
    const int seqCount = sequencer.process (bank, seqTransport, seqSettings, numSamples,
                                            seqScratch.data(), (int) seqScratch.size());
    count = mergeSequencerEvents (count, seqCount);

    // Merge into one time-ordered list (stable insertion sort).
    for (int i = 1; i < count; ++i)
    {
        const TriggerEvent tmp = eventScratch[(size_t) i];
        int j = i - 1;
        while (j >= 0 && eventScratch[(size_t) j].offset > tmp.offset)
        {
            eventScratch[(size_t) (j + 1)] = eventScratch[(size_t) j];
            --j;
        }
        eventScratch[(size_t) (j + 1)] = tmp;
    }

    for (int i = 0; i < count; ++i)
        uiTriggerCounts[eventScratch[(size_t) i].voice].fetch_add (1, std::memory_order_relaxed);

    writeMidiOutput (midi, seqScratch.data(), seqCount, numSamples);

    // Output routing: main bus plus any enabled aux buses.
    auto mainBus = getBusBuffer (buffer, false, 0);
    float* mainL = mainBus.getNumChannels() > 0 ? mainBus.getWritePointer (0) : buffer.getWritePointer (0);
    float* mainR = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : nullptr;
    if (mainR == nullptr)
    {
        monoFallback.setSize (1, numSamples, false, false, true);
        mainR = monoFallback.getWritePointer (0);
    }

    float* auxL[DrumEngine::kNumAuxBuses] {};
    float* auxR[DrumEngine::kNumAuxBuses] {};
    for (int b = 0; b < DrumEngine::kNumAuxBuses; ++b)
    {
        if (b + 1 >= getBusCount (false))
            break;
        auto* bus = getBus (false, b + 1);
        if (bus == nullptr || ! bus->isEnabled())
            continue;
        auto aux = getBusBuffer (buffer, false, b + 1);
        if (aux.getNumChannels() >= 2)
        {
            auxL[b] = aux.getWritePointer (0);
            auxR[b] = aux.getWritePointer (1);
        }
    }

    engine.process (eventScratch.data(), count, mainL, mainR, auxL, auxR, numSamples);

    // Clear any channels we did not write (unusual layouts).
    for (int ch = 2; ch < mainBus.getNumChannels(); ++ch)
        mainBus.clear (ch, 0, numSamples);

    // Never hand the host NaN or infinity. A voice that blew up is reset and
    // the block is muted; the next hits play normally.
    if (! outputIsFinite (buffer, numSamples))
    {
        engine.reset();
        buffer.clear();
    }

    uiStep.store (sequencer.currentStep(), std::memory_order_relaxed);
    uiPattern.store (sequencer.currentPattern(), std::memory_order_relaxed);
    uiPlaying.store (seqTransport.playing, std::memory_order_relaxed);
    for (int v = 0; v < kNumVoices; ++v)
        uiVoicePeaks[(size_t) v].store (engine.voicePeak (v), std::memory_order_relaxed);
    uiMasterPeaks[0].store (engine.masterPeakL(), std::memory_order_relaxed);
    uiMasterPeaks[1].store (engine.masterPeakR(), std::memory_order_relaxed);
}

// Appends the sequencer's events after the live ones (MIDI input and pads in
// eventScratch[0, liveCount)). A sequencer hit with a live hit on the same
// voice within kDuplicateWindowSeconds is dropped and the live hit keeps the
// louder of the two, so a MIDI clip recorded from the sequencer (or played
// along with it) does not double every hit. Returns the new event count.
int FloorplanProcessor::mergeSequencerEvents (int liveCount, int seqCount) noexcept
{
    const int window = (int) (kDuplicateWindowSeconds * getSampleRate());
    int count = liveCount;

    for (int i = 0; i < seqCount && count < (int) eventScratch.size(); ++i)
    {
        const auto& s = seqScratch[(size_t) i];
        bool duplicate = false;
        for (int j = 0; j < liveCount && ! duplicate; ++j)
        {
            auto& live = eventScratch[(size_t) j];
            if (live.voice == s.voice && std::abs (live.offset - s.offset) <= window)
            {
                duplicate = true;
                live.velocity = juce::jmax (live.velocity, s.velocity);
                live.accent = live.accent || s.accent;
            }
        }

        if (! duplicate)
            eventScratch[(size_t) count++] = s;
    }
    return count;
}

// Checks every main-bus sample and the last sample of each other channel: a
// voice whose state turns non-finite stays that way, so one sample per block
// is enough there. Uses the bit pattern, which fast-math cannot optimise away.
bool FloorplanProcessor::outputIsFinite (juce::AudioBuffer<float>& buffer, int numSamples) const noexcept
{
    auto finite = [] (float x) noexcept
    {
        uint32_t bits;
        std::memcpy (&bits, &x, sizeof (bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    };

    bool ok = true;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        if (ch < 2)
            for (int i = 0; i < numSamples; ++i)
                ok = ok && finite (data[i]);
        else
            ok = ok && finite (data[numSamples - 1]);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// GUI interaction
// ---------------------------------------------------------------------------
void FloorplanProcessor::triggerVoice (int voice, float velocity)
{
    const auto scope = triggerFifo.write (1);
    if (scope.blockSize1 > 0)
    {
        TriggerEvent e;
        e.voice = (uint8_t) juce::jlimit (0, kNumVoices - 1, voice);
        e.velocity = velocity;
        triggerQueue[(size_t) scope.startIndex1] = e;
    }
}

void FloorplanProcessor::resetVoiceNotes() noexcept
{
    for (int v = 0; v < kNumVoices; ++v)
        voiceNotes[(size_t) v].store (kVoiceSpecs[(size_t) v].defaultMidiNote);
}

int FloorplanProcessor::parameterIndex (const juce::String& paramId) const
{
    for (int i = 0; i < paramList.size(); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]))
            if (p->getParameterID() == paramId)
                return i;
    return -1;
}

void FloorplanProcessor::beginCcLearn (const juce::String& paramId)
{
    learnParam.store (parameterIndex (paramId));
}

void FloorplanProcessor::clearCcMapping (const juce::String& paramId)
{
    const int idx = parameterIndex (paramId);
    for (auto& c : ccToParam)
        if (c.load() == idx)
            c.store (-1);
}

int FloorplanProcessor::getCcForParameter (const juce::String& paramId) const
{
    const int idx = parameterIndex (paramId);
    for (int cc = 0; cc < 128; ++cc)
        if (idx >= 0 && ccToParam[(size_t) cc].load() == idx)
            return cc;
    return -1;
}

void FloorplanProcessor::timerCallback()
{
    sampleSlots.collectGarbage();

    const auto scope = uiFifo.read (uiFifo.getNumReady());
    auto handle = [this] (const MidiUiMessage& m)
    {
        switch (m.type)
        {
            case CcLearned:
                for (auto& c : ccToParam)
                    if (c.load() == m.b) c.store (-1); // one CC per parameter
                ccToParam[(size_t) juce::jlimit (0, 127, m.a)].store (m.b);
                break;
            case CcValue:
                if (auto* p = paramList[m.b])
                    p->setValueNotifyingHost (m.value);
                break;
            default:
                break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i) handle (uiQueue[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) handle (uiQueue[(size_t) (scope.startIndex2 + i)]);
}

// ---------------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------------
void FloorplanProcessor::applySampleOnMessageThread (int voice, std::unique_ptr<SampleData> data,
                                                     const juce::String& path, bool missing)
{
    {
        const juce::ScopedLock sl (sampleLock);
        samplePaths[(size_t) voice] = path;
        sampleMissing[(size_t) voice] = missing;
    }
    sampleSlots.publish (voice, data != nullptr ? std::move (data) : std::make_unique<SampleData>());
}

bool FloorplanProcessor::loadSample (int voice, const juce::File& file, juce::String& error)
{
    auto data = sampleLoader.load (file, error);
    if (data == nullptr)
        return false;
    applySampleOnMessageThread (voice, std::move (data), file.getFullPathName(), false);
    return true;
}

void FloorplanProcessor::clearSample (int voice)
{
    applySampleOnMessageThread (voice, nullptr, {}, false);
}

juce::String FloorplanProcessor::getSamplePath (int voice) const
{
    const juce::ScopedLock sl (sampleLock);
    return samplePaths[(size_t) voice];
}

bool FloorplanProcessor::isSampleMissing (int voice) const
{
    const juce::ScopedLock sl (sampleLock);
    return sampleMissing[(size_t) voice];
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void FloorplanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("FLOORPLAN");
    root.setAttribute ("stateVersion", kStateVersion);
    root.setAttribute ("pluginVersion", FLOORPLAN_VERSION_STRING);

    if (auto paramXml = apvts.copyState().createXml())
        root.addChildElement (paramXml.release());

    root.createNewChildElement ("PATTERNS")->addTextElement (serial::bankToJson (patterns.getBank()));

    auto* samples = root.createNewChildElement ("SAMPLES");
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto path = getSamplePath (v);
        if (path.isNotEmpty())
        {
            auto* s = samples->createNewChildElement ("SAMPLE");
            s->setAttribute ("voice", kVoiceSpecs[(size_t) v].id);
            s->setAttribute ("path", path); // reference only: audio data is never stored in the project
        }
    }

    auto* midiMap = root.createNewChildElement ("MIDI");
    for (int v = 0; v < kNumVoices; ++v)
        midiMap->setAttribute (juce::String (kVoiceSpecs[(size_t) v].id) + "_note", voiceNotes[(size_t) v].load());
    for (int cc = 0; cc < 128; ++cc)
    {
        const int idx = ccToParam[(size_t) cc].load();
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (paramList[idx]))
        {
            auto* m = midiMap->createNewChildElement ("CC");
            m->setAttribute ("cc", cc);
            m->setAttribute ("param", p->getParameterID());
        }
    }

    copyXmlToBinary (root, destData);
}

void FloorplanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    // Version 0: a bare APVTS tree (early development builds). Parameters only.
    if (xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (withMissingParametersAtDefault (juce::ValueTree::fromXml (*xml), *this));
        return;
    }

    if (! xml->hasTagName ("FLOORPLAN"))
        return;

    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (withMissingParametersAtDefault (juce::ValueTree::fromXml (*p), *this));

    if (auto* pat = xml->getChildByName ("PATTERNS"))
        if (auto bank = serial::bankFromJson (pat->getAllSubText()))
            patterns.setBank (*bank);

    if (auto* midiMap = xml->getChildByName ("MIDI"))
    {
        for (int v = 0; v < kNumVoices; ++v)
            voiceNotes[(size_t) v].store (juce::jlimit (0, 127, midiMap->getIntAttribute (
                juce::String (kVoiceSpecs[(size_t) v].id) + "_note", kVoiceSpecs[(size_t) v].defaultMidiNote)));

        for (auto& c : ccToParam) c.store (-1);
        for (auto* m : midiMap->getChildWithTagNameIterator ("CC"))
        {
            const int cc = m->getIntAttribute ("cc", -1);
            const int idx = parameterIndex (m->getStringAttribute ("param"));
            if (juce::isPositiveAndBelow (cc, 128) && idx >= 0)
                ccToParam[(size_t) cc].store (idx);
        }
    }

    // Samples: reload from their paths; a missing file falls back to synthesis.
    for (int v = 0; v < kNumVoices; ++v)
    {
        juce::String path;
        if (auto* samples = xml->getChildByName ("SAMPLES"))
            for (auto* s : samples->getChildWithTagNameIterator ("SAMPLE"))
                if (s->getStringAttribute ("voice") == kVoiceSpecs[(size_t) v].id)
                    path = s->getStringAttribute ("path");

        if (path.isEmpty())
        {
            if (getSamplePath (v).isNotEmpty())
                clearSample (v);
            continue;
        }

        // A moved project or a different machine: look for the file by name
        // in the samples folder. If it is still not found, the saved path is
        // kept, so saving again does not lose the reference.
        const juce::File found = findMissingSample (path);
        juce::String error;
        auto loaded = found != juce::File() ? sampleLoader.load (found, error) : nullptr;
        const bool missing = loaded == nullptr;
        applySampleOnMessageThread (v, std::move (loaded), missing ? path : found.getFullPathName(), missing);
    }
}

juce::File FloorplanProcessor::getDefaultSampleFolder()
{
    return PresetManager::getUserFolder().getSiblingFile ("Samples");
}

juce::File FloorplanProcessor::findMissingSample (const juce::String& savedPath) const
{
    if (juce::File::isAbsolutePath (savedPath) && juce::File (savedPath).existsAsFile())
        return juce::File (savedPath);

    // Saved on another OS, the path may use the other separator.
    const auto name = savedPath.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    if (name.isEmpty() || ! sampleSearchFolder.isDirectory())
        return {};

    for (const auto& entry : juce::RangedDirectoryIterator (sampleSearchFolder, true, "*", juce::File::findFiles))
        if (entry.getFile().getFileName().equalsIgnoreCase (name))
            return entry.getFile();
    return {};
}

juce::AudioProcessorEditor* FloorplanProcessor::createEditor()
{
    return new FloorplanEditor (*this);
}

} // namespace fp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fp::FloorplanProcessor();
}
