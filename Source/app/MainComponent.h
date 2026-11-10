// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "ui/LevelMeter.h"

namespace encore
{

/** Milestone 0 window: device setup, guitar input choice, meters, latency,
    CPU/dropout counters and the master volume/mute. The real performance and
    edit views replace this in later milestones; the engine underneath stays.
*/
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (AppSettings& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void openAudioSettings();
    void refreshInputChoices();
    void refreshStatus();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    AppSettings& settings;

    // Order matters: the engine must outlive the device manager that calls it.
    AudioEngine engine;
    juce::AudioDeviceManager deviceManager;

    juce::Label title, deviceStatus, latencyStatus, healthStatus, warning;
    juce::TextButton audioSettingsButton { "Audio settings..." };
    juce::TextButton muteButton { "MUTED" };
    juce::TextButton resetCountersButton { "Reset counters" };
    juce::Label inputLabel { {}, "Guitar input" };
    juce::ComboBox inputChoice;
    juce::Label volumeLabel { {}, "Master volume" };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LevelMeter inputMeter { "Input" }, outputMeter { "Output" };

    int frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace encore
