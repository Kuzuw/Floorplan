#pragma once

#include "Instruments/SampleVoice.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace fp
{

// Decodes an audio file into an immutable SampleData. Never call this from
// the audio thread: it reads from disk and allocates.
class SampleLoader
{
public:
    static constexpr double kMaxSeconds = 30.0;

    SampleLoader();

    std::unique_ptr<SampleData> load (const juce::File& file, juce::String& error);
    bool canLoad (const juce::String& path) const;
    juce::String getWildcard() const { return formats.getWildcardForAllFormats(); }

private:
    juce::AudioFormatManager formats;
};

} // namespace fp
