#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fp
{

// Kit presets: the sound of all eight voices plus the master section.
// Patterns are deliberately not part of a kit so switching sounds never
// destroys a groove. Factory kits live in code; user kits are XML files
// (*.fpkit) in the user preset folder. Message thread only.
class PresetManager
{
public:
    static constexpr const char* kExtension = ".fpkit";
    static constexpr int kKitFormatVersion = 1;

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    juce::StringArray getNames() const;   // factory first, then user kits
    int getNumFactory() const;
    int getCurrentIndex() const noexcept { return current; }
    juce::String getCurrentName() const;

    void load (int index);
    void next();
    void previous();
    bool saveUser (const juce::String& name);
    bool loadFromFile (const juce::File& file);
    void refreshUserList();

    static juce::File getUserFolder();
    static juce::File getUserPatternFolder();

private:
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);
    void resetKitParameters();

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<juce::File> userFiles;
    int current = 0;
};

} // namespace fp
