// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace encore
{

/** Per-machine settings: audio device, buffer size, guitar input, master volume.

    These live in %APPDATA%\Encore\Encore.settings, never inside a show, so the
    same show folder runs on any computer (see the architecture doc).
    Message thread only.
*/
class AppSettings
{
public:
    AppSettings();

    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement* state);

    int getGuitarInputChannel() const;
    void setGuitarInputChannel (int channel);

    float getMasterGainDecibels() const;
    void setMasterGainDecibels (float dB);

    /** Writes to disk now (also happens automatically on change). */
    void save();

private:
    juce::PropertiesFile& file() const;

    mutable juce::ApplicationProperties properties;
};

} // namespace encore
