// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "platform/SystemTuning.h"
#include "ui/kit/Group.h"
#include "ui/kit/SegmentedControl.h"

namespace encore
{

/** The Audio tab of the settings window (SCREENS.md §7): the driver type,
    device, sample rate, buffer size and active channels, set straight on the
    AudioDeviceManager (what JUCE's stock selector let you change, drawn as
    the design), the latency, Windows live tuning, the status and a tip.
    Changes apply immediately, as before. */
class AudioSettingsView final : public juce::Component,
                                private juce::ChangeListener
{
public:
    /** The status lines (the main window's health data, refreshed by its timer). */
    struct Status
    {
        juce::String cpu, overruns, driverDropouts, limiter, memory, latency;
        bool operator== (const Status&) const = default;
    };

    AudioSettingsView (juce::AudioDeviceManager&, const system::TuningReport&, std::function<void()> resetCounters);
    ~AudioSettingsView() override;

    void setStatus (const Status&);

    /** Height the content needs at its width (the window scrolls when smaller). */
    int getIdealHeight() const;

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();   // the controls from the device manager
    void apply (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change);
    int deviceBoxHeight() const;
    int channelsBoxHeight() const;

    juce::AudioDeviceManager& deviceManager;
    const system::TuningReport tuning;

    kit::Group deviceGroup { "Device" }, channelsGroup { "Active channels" }, tuningGroup { juce::String (juce::CharPointer_UTF8 ("Windows live tuning \xc2\xb7 automatic")) },
               statusGroup { "Status" }, tipGroup { "Tip" };

    kit::SegmentedControl driverTypes, bufferSizes;
    juce::ComboBox outputDevice, inputDevice, sampleRate, moreSizes;
    juce::TextButton controlPanel { "Control panel" }, resetCounters { "Reset counters" };
    juce::OwnedArray<juce::ToggleButton> inputs, outputs;
    std::vector<int> shownSizes;

    // Drawn text, made when it changes (no strings in paint()).
    Status status;
    juce::String statusTitle, statusLine, latencyLead, latencyRoundTrip, latencyTail;
    float inMs = 0.0f, outMs = 0.0f;
    bool deviceOpen = false, separateInputs = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsView)
};

} // namespace encore
