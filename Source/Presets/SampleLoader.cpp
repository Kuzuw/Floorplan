#include "Presets/SampleLoader.h"

namespace fp
{

SampleLoader::SampleLoader()
{
    formats.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg (JUCE built-ins)
}

bool SampleLoader::canLoad (const juce::String& path) const
{
    return formats.findFormatForFileExtension (juce::File (path).getFileExtension()) != nullptr;
}

std::unique_ptr<SampleData> SampleLoader::load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported audio file: " + file.getFileName();
        return nullptr;
    }

    const auto maxLength = (juce::int64) (reader->sampleRate * kMaxSeconds);
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    if (length < 2 || reader->sampleRate <= 0.0)
    {
        error = "Audio file is empty: " + file.getFileName();
        return nullptr;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buffer (channels, length);
    reader->read (&buffer, 0, length, 0, true, channels > 1);

    auto data = std::make_unique<SampleData>();
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName().toStdString();
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
    if (channels > 1)
        data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
    return data;
}

} // namespace fp
