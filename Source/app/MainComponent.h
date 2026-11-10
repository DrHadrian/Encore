// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <map>

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "ui/BlockPanel.h"
#include "ui/LevelMeter.h"

namespace encore
{

/** Development window: device setup, guitar input, meters, latency, the
    current chain's blocks, CPU/dropout counters and the master volume/mute.
    The real performance and edit views replace this in later milestones; the
    engine underneath stays.
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

    /** Height needed to show everything at this width (for the scrolling window). */
    int getRequiredHeight (int width);

private:
    void openAudioSettings();
    void refreshInputChoices();
    void refreshStatus();
    void buildDefaultChain();
    void rebuildBlockPanels();
    void restoreLastFiles();
    void relayout();
    int layoutContent (int width, bool apply);

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
    juce::Label chainLabel { {}, "Chain" };
    std::vector<std::unique_ptr<BlockPanel>> blockPanels;
    std::map<BlockPanel*, Block*> panelBlocks;
    Chain* shownChain = nullptr;

    int frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace encore
