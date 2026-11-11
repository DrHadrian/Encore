// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/AppSettings.h"

namespace encore
{

namespace keys
{
    static constexpr auto audioDevice = "audioDeviceState";
    static constexpr auto guitarInput = "guitarInputChannel";
    static constexpr auto masterGain  = "masterGainDb";
    static constexpr auto lastFile    = "lastFile_";
    static constexpr auto browseDir   = "lastBrowseFolder";
    static constexpr auto lastShow    = "lastShowFolder";
    static constexpr auto midiMap     = "midiBindings";
    static constexpr auto midiOut     = "midiOutput";
    static constexpr auto layoutInspector = "layoutEditorInspector";
}

AppSettings::AppSettings() : AppSettings (juce::File()) {}

AppSettings::AppSettings (const juce::File& folder)
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Encore";
    // An absolute folder name is used as it is (JUCE's getChildFile).
    options.folderName          = folder == juce::File() ? juce::String ("Encore") : folder.getFullPathName();
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat       = juce::PropertiesFile::storeAsXML;   // human-readable for support
    options.millisecondsBeforeSaving = 500;                           // debounced autosave
    options.processLock         = nullptr;

    properties.setStorageParameters (options);
}

juce::PropertiesFile& AppSettings::file() const
{
    // getUserSettings() never returns null once storage parameters are set.
    return *properties.getUserSettings();
}

std::unique_ptr<juce::XmlElement> AppSettings::getAudioDeviceState() const
{
    return file().getXmlValue (keys::audioDevice);
}

void AppSettings::setAudioDeviceState (const juce::XmlElement* state)
{
    if (state != nullptr)
        file().setValue (keys::audioDevice, state);
}

int AppSettings::getGuitarInputChannel() const
{
    return file().getIntValue (keys::guitarInput, 0);
}

void AppSettings::setGuitarInputChannel (int channel)
{
    file().setValue (keys::guitarInput, channel);
}

float AppSettings::getMasterGainDecibels() const
{
    return (float) file().getDoubleValue (keys::masterGain, -12.0);
}

void AppSettings::setMasterGainDecibels (float dB)
{
    file().setValue (keys::masterGain, (double) dB);
}

bool AppSettings::getLayoutInspectorShown() const
{
    return file().getBoolValue (keys::layoutInspector, true);
}

void AppSettings::setLayoutInspectorShown (bool shown)
{
    file().setValue (keys::layoutInspector, shown);
}

juce::File AppSettings::getLastFile (const juce::String& blockTypeId) const
{
    const auto path = file().getValue (keys::lastFile + blockTypeId);
    return path.isEmpty() ? juce::File() : juce::File (path);
}

void AppSettings::setLastFile (const juce::String& blockTypeId, const juce::File& f)
{
    file().setValue (keys::lastFile + blockTypeId, f.getFullPathName());
}

juce::File AppSettings::getFolder() const
{
    return file().getFile().getParentDirectory();
}

std::vector<MidiBinding> AppSettings::getMidiBindings() const
{
    const auto text = file().getValue (keys::midiMap);

    if (text.isEmpty())
        return makeDefaultMidiBindings();

    return bindingsFromVar (juce::JSON::parse (text));
}

void AppSettings::setMidiBindings (const std::vector<MidiBinding>& bindings)
{
    file().setValue (keys::midiMap, juce::JSON::toString (bindingsToVar (bindings), true));
}

juce::String AppSettings::getMidiOutput() const               { return file().getValue (keys::midiOut); }
void AppSettings::setMidiOutput (const juce::String& id)      { file().setValue (keys::midiOut, id); }

juce::File AppSettings::getLastShowFolder() const
{
    const auto path = file().getValue (keys::lastShow);
    return path.isEmpty() ? juce::File() : juce::File (path);
}

void AppSettings::setLastShowFolder (const juce::File& folder)
{
    file().setValue (keys::lastShow, folder.getFullPathName());
}

juce::File AppSettings::getLastBrowseFolder() const
{
    const juce::File folder (file().getValue (keys::browseDir));
    return folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

void AppSettings::setLastBrowseFolder (const juce::File& folder)
{
    file().setValue (keys::browseDir, folder.getFullPathName());
}

void AppSettings::save()
{
    file().saveIfNeeded();
}

} // namespace encore
