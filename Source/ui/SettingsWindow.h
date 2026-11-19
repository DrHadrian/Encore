// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/AudioSettingsView.h"
#include "ui/MidiSettings.h"
#include "ui/PluginSettings.h"
#include "ui/kit/DialogFrame.h"

namespace encore
{

/** Settings — Audio | MIDI | Plugins (SCREENS.md §7): one dialog over the
    main window with a tab each. Changes apply immediately (as the two separate windows
    did), so Done and Cancel both just close it; so do Escape and ✕. */
class SettingsWindow final
{
public:
    enum class Tab { audio, midi, plugins };

    SettingsWindow (juce::AudioDeviceManager&, const system::TuningReport&, std::function<void()> resetCounters,
                    MidiRouter&, AppSettings&, ShowSession&, PluginCatalog&,
                    std::function<void()> applyBindings, std::function<void()> applyOutput,
                    std::function<void()> close);
    ~SettingsWindow();

    /** The overlay to add over the main window (dims it, holds the dialog). */
    juce::Component& getOverlay() noexcept { return *overlay; }

    void showTab (Tab);
    Tab getTab() const noexcept { return tab; }

    /** From the main window's timer, a few times a second. */
    void setAudioStatus (const AudioSettingsView::Status&);

private:
    class Body;

    kit::DialogFrame frame;
    AudioSettingsView audio;
    MidiSettings midi;
    PluginSettings plugins;
    std::unique_ptr<Body> body;
    std::unique_ptr<kit::DialogOverlay> overlay;
    Tab tab = Tab::audio;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};

} // namespace encore
