#pragma once

#include "Audio/DrumEngine.h"
#include "Parameters/ParameterLayout.h"
#include "Presets/PatternStore.h"
#include "Presets/PresetManager.h"
#include "Presets/SampleLoader.h"
#include "Sequencer/Sequencer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

namespace fp
{

// Thread ownership summary (see docs/ARCHITECTURE.md for details):
//  - Audio thread: engine, sequencer, eventScratch, MIDI note-off tracking.
//  - Message thread: preset manager, sample loading, MIDI-learn bookkeeping.
//  - Shared: parameters (APVTS atomics), patterns (PatternStore triple
//    buffer), GUI triggers and MIDI-learn messages (lock-free FIFOs), samples
//    (SampleSlots), meters and play position (relaxed atomics).
class FloorplanProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kStateVersion = 1;

    FloorplanProcessor();
    ~FloorplanProcessor() override;

    // AudioProcessor ---------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Shared objects -----------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    PatternStore patterns;
    PresetManager presets { apvts };

    // GUI -> audio -------------------------------------------------------------
    void triggerVoice (int voice, float velocity);    // audition (lock-free)
    void setInternalPlaying (bool shouldPlay) noexcept { internalPlay.store (shouldPlay); }
    bool isInternalPlaying() const noexcept { return internalPlay.load(); }

    // Samples (message thread) ---------------------------------------------------
    bool loadSample (int voice, const juce::File& file, juce::String& error);
    void clearSample (int voice);
    juce::String getSamplePath (int voice) const;
    bool isSampleMissing (int voice) const;
    juce::String getSampleWildcard() const { return sampleLoader.getWildcard(); }
    bool canLoadSample (const juce::String& path) const { return sampleLoader.canLoad (path); }

    // Where a project's sample is looked for by file name when its saved path
    // no longer exists (default: Documents/Floorplan/Samples, searched recursively).
    static juce::File getDefaultSampleFolder();
    void setSampleSearchFolder (const juce::File& folder) { sampleSearchFolder = folder; }
    juce::File getSampleSearchFolder() const { return sampleSearchFolder; }

    // Hits from MIDI input or the pads within this window of a sequencer hit on
    // the same voice replace it instead of doubling it.
    static constexpr double kDuplicateWindowSeconds = 0.002;

    // MIDI mapping (message thread) ------------------------------------------------
    void beginNoteLearn (int voice) noexcept { learnNoteVoice.store (voice); }
    int getVoiceNote (int voice) const noexcept { return voiceNotes[(size_t) voice].load(); }
    void resetVoiceNotes() noexcept;
    void beginCcLearn (const juce::String& paramId);
    void clearCcMapping (const juce::String& paramId);
    int getCcForParameter (const juce::String& paramId) const;
    bool isLearning() const noexcept { return learnParam.load() >= 0 || learnNoteVoice.load() >= 0; }

    // Audio -> GUI (relaxed atomics, polled by the editor) --------------------------
    int getPlayingStep() const noexcept       { return uiStep.load (std::memory_order_relaxed); }
    int getPlayingPattern() const noexcept    { return uiPattern.load (std::memory_order_relaxed); }
    bool isTransportRunning() const noexcept  { return uiPlaying.load (std::memory_order_relaxed); }
    bool isHostSynced() const noexcept        { return uiHostSync.load (std::memory_order_relaxed); }
    double getDisplayBpm() const noexcept     { return uiBpm.load (std::memory_order_relaxed); }
    float getVoicePeak (int v) const noexcept { return uiVoicePeaks[(size_t) v].load (std::memory_order_relaxed); }
    float getMasterPeak (int ch) const noexcept { return uiMasterPeaks[(size_t) ch].load (std::memory_order_relaxed); }
    uint32_t getTriggerCount (int v) const noexcept { return uiTriggerCounts[(size_t) v].load (std::memory_order_relaxed); }
    float getCpuLoad() const noexcept         { return (float) loadMeasurer.getLoadAsProportion(); }

private:
    struct MidiUiMessage { int type; int a; int b; float value; }; // audio -> message thread
    enum MidiUiType { CcValue, CcLearned, NoteLearned };

    static BusesProperties makeBuses();
    void timerCallback() override;
    void handleMidiInput (const juce::MidiBuffer& midi, int numSamples, int& count);
    TransportState readTransport (int numSamples);
    void writeMidiOutput (juce::MidiBuffer& midi, const TriggerEvent* seqEvents, int numSeqEvents, int numSamples);
    void pushUiMessage (const MidiUiMessage& m) noexcept;
    void applySampleOnMessageThread (int voice, std::unique_ptr<SampleData> data, const juce::String& path, bool missing);
    juce::File findMissingSample (const juce::String& savedPath) const;
    int mergeSequencerEvents (int liveCount, int seqCount) noexcept;
    bool outputIsFinite (juce::AudioBuffer<float>& buffer, int numSamples) const noexcept;
    int parameterIndex (const juce::String& paramId) const;

    ParameterCache params { apvts };
    DrumEngine engine;
    Sequencer sequencer;
    SampleSlots sampleSlots;
    SampleLoader sampleLoader;
    juce::AudioProcessLoadMeasurer loadMeasurer;

    // Audio-thread state
    std::array<TriggerEvent, Sequencer::kMaxEventsPerBlock> eventScratch {};
    std::array<TriggerEvent, Sequencer::kMaxEventsPerBlock> seqScratch {};
    SequencerSettings seqSettings;
    double internalPpq = 0.0;
    double lastHostBpm = 120.0;
    bool hostWasPlaying = false;

    // MIDI output note-offs still to send. The note is kept with the countdown
    // so a note-map change in between cannot leave a note hanging.
    struct PendingNoteOff { int samplesLeft = -1; int note = 0; };
    std::array<PendingNoteOff, kNumVoices> pendingNoteOff {};
    juce::AudioBuffer<float> monoFallback;

    // GUI -> audio trigger queue
    juce::AbstractFifo triggerFifo { 128 };
    std::array<TriggerEvent, 128> triggerQueue {};

    // Audio -> message MIDI-learn queue
    juce::AbstractFifo uiFifo { 512 };
    std::array<MidiUiMessage, 512> uiQueue {};

    // MIDI mapping
    std::array<std::atomic<int>, kNumVoices> voiceNotes {};
    std::array<std::atomic<int>, 128> ccToParam {};
    std::atomic<int> learnParam { -1 };
    std::atomic<int> learnNoteVoice { -1 };
    juce::Array<juce::AudioProcessorParameter*> paramList;

    // Samples (message thread, guarded for state calls from other threads)
    mutable juce::CriticalSection sampleLock;
    std::array<juce::String, kNumVoices> samplePaths;
    std::array<bool, kNumVoices> sampleMissing {};
    juce::File sampleSearchFolder { getDefaultSampleFolder() };

    std::atomic<bool> internalPlay { false };

    // UI feedback
    std::atomic<int> uiStep { -1 }, uiPattern { 0 };
    std::atomic<bool> uiPlaying { false }, uiHostSync { false };
    std::atomic<double> uiBpm { 124.0 };
    std::array<std::atomic<float>, kNumVoices> uiVoicePeaks {};
    std::array<std::atomic<float>, 2> uiMasterPeaks {};
    std::array<std::atomic<uint32_t>, kNumVoices> uiTriggerCounts {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloorplanProcessor)
};

} // namespace fp
