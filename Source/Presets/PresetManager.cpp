#include "Presets/PresetManager.h"
#include "Parameters/ParameterLayout.h"
#include "Presets/FactoryContent.h"

namespace fp
{

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    refreshUserList();
}

juce::File PresetManager::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Floorplan").getChildFile ("Kits");
}

juce::File PresetManager::getUserPatternFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Floorplan").getChildFile ("Patterns");
}

void PresetManager::refreshUserList()
{
    userFiles.clear();
    const auto folder = getUserFolder();
    if (folder.isDirectory())
        userFiles = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);
    std::sort (userFiles.begin(), userFiles.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
}

int PresetManager::getNumFactory() const
{
    return (int) factory::kits().size();
}

juce::StringArray PresetManager::getNames() const
{
    juce::StringArray names;
    for (const auto& k : factory::kits())
        names.add (k.name);
    for (const auto& f : userFiles)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

juce::String PresetManager::getCurrentName() const
{
    const auto names = getNames();
    return juce::isPositiveAndBelow (current, names.size()) ? names[current] : juce::String();
}

void PresetManager::resetKitParameters()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isKitParameter (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PresetManager::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    for (const auto& [id, value] : values)
        if (auto* p = apvts.getParameter (id))
            if (isKitParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::load (int index)
{
    const int numFactory = getNumFactory();
    const int total = numFactory + userFiles.size();
    if (total == 0)
        return;

    index = ((index % total) + total) % total;

    if (index < numFactory)
    {
        resetKitParameters();
        applyValues (factory::kits()[(size_t) index].values);
        current = index;
    }
    else if (loadFromFile (userFiles[index - numFactory]))
    {
        current = index;
    }
}

void PresetManager::next()     { load (current + 1); }
void PresetManager::previous() { load (current - 1); }

bool PresetManager::loadFromFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("FloorplanKit"))
        return false;

    std::vector<std::pair<juce::String, float>> values;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        values.emplace_back (e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value"));

    resetKitParameters(); // parameters missing from older files fall back to defaults
    applyValues (values);
    return true;
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    juce::XmlElement xml ("FloorplanKit");
    xml.setAttribute ("version", kKitFormatVersion);
    xml.setAttribute ("name", name);

    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isKitParameter (rp->getParameterID()))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
            }

    const auto folder = getUserFolder();
    if (! folder.createDirectory())
        return false;

    const auto file = folder.getChildFile (name + kExtension);
    if (! xml.writeTo (file))
        return false;

    refreshUserList();
    const int idx = userFiles.indexOf (file);
    if (idx >= 0)
        current = getNumFactory() + idx;
    return true;
}

} // namespace fp
