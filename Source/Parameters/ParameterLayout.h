#pragma once

#include "Audio/DrumEngine.h"
#include "Sequencer/Sequencer.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace fp
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// True for parameters that belong to a "kit" preset (sound, not sequencing).
bool isKitParameter (const juce::String& paramId);

// Raw pointers to the parameter atomics, resolved once so the audio thread
// can read every parameter per block without string lookups or locks.
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& state);

    void readEngine (DrumEngine& engine) const noexcept;
    void readSequencer (SequencerSettings& settings) const noexcept;

    float tempo() const noexcept      { return tempoParam->load (std::memory_order_relaxed); }
    bool midiOutEnabled() const noexcept { return midiOutParam->load (std::memory_order_relaxed) > 0.5f; }

private:
    struct VoicePtrs
    {
        std::array<std::atomic<float>*, kNumSynthParams> synth {};
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* filter = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* sampleTune = nullptr;
        std::atomic<float>* sampleDecay = nullptr;
    };

    std::array<VoicePtrs, kNumVoices> voices;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* clip = nullptr;
    std::atomic<float>* comp = nullptr;
    std::atomic<float>* accentParam = nullptr;
    std::atomic<float>* swing = nullptr;
    std::atomic<float>* groove = nullptr;
    std::atomic<float>* resolution = nullptr;
    std::atomic<float>* pattern = nullptr;
    std::atomic<float>* chainOn = nullptr;
    std::array<std::atomic<float>*, kChainSlots> chain {};
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<float>* seed = nullptr;
    std::atomic<float>* midiOutParam = nullptr;
};

} // namespace fp
