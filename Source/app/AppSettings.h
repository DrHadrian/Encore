// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "model/MidiMapping.h"

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

    /** Settings kept in `folder` instead of the user's app-data folder (tests). */
    explicit AppSettings (const juce::File& folder);

    /** %APPDATA%\Encore: settings, logs, the crash recovery file. */
    juce::File getFolder() const;

    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement* state);

    int getGuitarInputChannel() const;
    void setGuitarInputChannel (int channel);

    float getMasterGainDecibels() const;
    void setMasterGainDecibels (float dB);

    /** The layout editor's widget inspector (Inspector toggle, key I). */
    bool getLayoutInspectorShown() const;
    void setLayoutInspectorShown (bool shown);

    /** Last file loaded into a block type (e.g. the amp), reloaded at start-up
        until shows and patches (milestone 4) remember this properly. */
    juce::File getLastFile (const juce::String& blockTypeId) const;
    void setLastFile (const juce::String& blockTypeId, const juce::File& file);

    /** The show open when Encore last closed. */
    juce::File getLastShowFolder() const;
    void setLastShowFolder (const juce::File& folder);

    /** This computer's MIDI mappings (defaults until changed). */
    std::vector<MidiBinding> getMidiBindings() const;
    void setMidiBindings (const std::vector<MidiBinding>& bindings);

    /** The MIDI output cue messages go to (device identifier, empty = none). */
    juce::String getMidiOutput() const;
    void setMidiOutput (const juce::String& identifier);

    /** Where file choosers open. */
    juce::File getLastBrowseFolder() const;
    void setLastBrowseFolder (const juce::File& folder);

    /** Writes to disk now (also happens automatically on change). */
    void save();

private:
    juce::PropertiesFile& file() const;

    mutable juce::ApplicationProperties properties;
};

} // namespace encore
