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
}

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Encore";
    options.folderName          = "Encore";
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

juce::File AppSettings::getLastFile (const juce::String& blockTypeId) const
{
    const auto path = file().getValue (keys::lastFile + blockTypeId);
    return path.isEmpty() ? juce::File() : juce::File (path);
}

void AppSettings::setLastFile (const juce::String& blockTypeId, const juce::File& f)
{
    file().setValue (keys::lastFile + blockTypeId, f.getFullPathName());
}

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
