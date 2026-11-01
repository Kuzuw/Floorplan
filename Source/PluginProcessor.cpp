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

    // Optional per-voice outputs for multi-out routing in a DAW. The shared
    // code is compiled once for all formats, so check the wrapper at runtime.
    if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_Standalone)
        for (int v = 0; v < kNumVoices; ++v)
            buses = buses.withOutput (juce::String ("Out ") + juce::String (v + 2) + " (" + kVoiceSpecs[(size_t) v].name + ")",
                                      juce::AudioChannelSet::stereo(), false);
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
    pendingNoteOff.fill (-1);
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
    pendingNoteOff.fill (-1);
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
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
        }
    }

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
        t.playing = true;
        t.bpm = params.tempo();
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

void FloorplanProcessor::handleMidiInput (const juce::MidiBuffer& midi, int numSamples, int& count)
{
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
            }
        }
        else if (msg.isController())
        {
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
    for (int v = 0; v < kNumVoices; ++v)
    {
        int& left = pendingNoteOff[(size_t) v];
        if (left < 0)
            continue;
        if (left < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, voiceNotes[(size_t) v].load()), left);
            left = -1;
        }
        else
        {
            left -= numSamples;
        }
    }

    if (! params.midiOutEnabled())
        return;

    const int noteLength = juce::jmax (1, (int) (kMidiNoteLength * getSampleRate()));
    for (int i = 0; i < numSeqEvents; ++i)
    {
        const auto& e = seqEvents[i];
        const int note = voiceNotes[(size_t) e.voice].load();
        int& pending = pendingNoteOff[(size_t) e.voice];
        if (pending >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, note), e.offset);

        const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (e.velocity * 127.0f) + (e.accent ? 10 : 0));
        midi.addEvent (juce::MidiMessage::noteOn (kMidiOutChannel, note, vel), e.offset);

        const int offAt = e.offset + noteLength;
        if (offAt < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (kMidiOutChannel, note), offAt);
            pending = -1;
        }
        else
        {
            pending = offAt - numSamples;
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
    handleMidiInput (midi, numSamples, count);

    // 2. Pads clicked / keys pressed in the editor (always at block start).
    {
        const auto scope = triggerFifo.read (triggerFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1 && count < (int) eventScratch.size(); ++i)
            eventScratch[(size_t) count++] = triggerQueue[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2 && count < (int) eventScratch.size(); ++i)
            eventScratch[(size_t) count++] = triggerQueue[(size_t) (scope.startIndex2 + i)];
    }

    // 3. Sequencer.
    const PatternBank& bank = patterns.acquireForAudio();
    const int seqCount = sequencer.process (bank, transport, seqSettings, numSamples,
                                            seqScratch.data(), (int) seqScratch.size());
    for (int i = 0; i < seqCount && count < (int) eventScratch.size(); ++i)
        eventScratch[(size_t) count++] = seqScratch[(size_t) i];

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

    uiStep.store (sequencer.currentStep(), std::memory_order_relaxed);
    uiPattern.store (sequencer.currentPattern(), std::memory_order_relaxed);
    uiPlaying.store (transport.playing, std::memory_order_relaxed);
    for (int v = 0; v < kNumVoices; ++v)
        uiVoicePeaks[(size_t) v].store (engine.voicePeak (v), std::memory_order_relaxed);
    uiMasterPeaks[0].store (engine.masterPeakL(), std::memory_order_relaxed);
    uiMasterPeaks[1].store (engine.masterPeakR(), std::memory_order_relaxed);
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

        juce::String error;
        auto loaded = sampleLoader.load (juce::File (path), error);
        const bool missing = loaded == nullptr;
        applySampleOnMessageThread (v, std::move (loaded), path, missing);
    }
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
