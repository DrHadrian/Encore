// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/AppSettings.h"
#include "engine/midi/MidiRouter.h"
#include "session/ShowSession.h"

namespace encore
{

/** MIDI setup: which devices Encore listens to, where cue messages go, and
    the mappings from footswitches and pedals to actions, with MIDI learn
    (click Learn, press the control). Mappings belong to this computer unless
    the show is set to carry its own. */
class MidiSettings final : public juce::Component,
                           private juce::Timer
{
public:
    /** applyBindings: push the effective mappings to the router.
        applyOutput: (re)open the chosen MIDI output. */
    MidiSettings (juce::AudioDeviceManager&, MidiRouter&, AppSettings&, ShowSession&,
                  std::function<void()> applyBindings, std::function<void()> applyOutput);
    ~MidiSettings() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    void timerCallback() override;
    void rebuildInputs();
    void rebuildOutputs();
    void rebuildRows();
    void showAddMenu();

    std::vector<MidiBinding> currentBindings() const;
    void storeBindings (const std::vector<MidiBinding>& bindings);
    void startLearning (int row);

    juce::AudioDeviceManager& deviceManager;
    MidiRouter& router;
    AppSettings& settings;
    ShowSession& session;
    std::function<void()> applyBindings, applyOutput;

    juce::Label inputsLabel { {}, "Listen to" }, outputLabel { {}, "Send cue messages to" },
                mappingsLabel { {}, "Mappings" }, lastLabel;
    juce::OwnedArray<juce::ToggleButton> inputButtons;
    juce::Array<juce::MidiDeviceInfo> shownInputs;
    juce::ComboBox outputChoice;
    juce::Array<juce::MidiDeviceInfo> shownOutputs;
    juce::ToggleButton showOwnMappings { "This show has its own mappings (instead of this computer's)" };
    juce::TextButton addButton { "+ Add mapping" }, resetButton { "Reset to defaults" };

    juce::Viewport rowsViewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;

    void stopLearning();

    int learningRow = -1, learnStartSequence = 0;
    juce::uint32 learnStarted = 0;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSettings)
};

} // namespace encore
