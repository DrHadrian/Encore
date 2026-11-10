// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "platform/SystemTuning.h"
#include "session/ShowSession.h"
#include "ui/BlockPanel.h"
#include "ui/ChainStrip.h"
#include "ui/LevelMeter.h"
#include "ui/MidiSettings.h"
#include "ui/PatchList.h"
#include "ui/SetList.h"
#include "ui/StageView.h"
#include "ui/TunerDisplay.h"

namespace encore
{

/** The edit view: the show's patches on the left, the selected patch's chain
    and the selected block's controls on the right, audio status at the bottom.
    F5 (or "Stage") switches to the stage view, which covers the window.
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
    bool keyPressed (const juce::KeyPress&) override;

    /** Height needed to show everything at this width (for the scrolling window). */
    int getRequiredHeight (int width);

    /** The stage view fills the window instead of scrolling: the window
        re-lays out when it's switched. */
    std::function<void()> onViewChanged;
    bool isShowingStage() const noexcept { return stageView != nullptr && stageView->isVisible(); }

private:
    // Audio
    void openAudioSettings();
    void refreshInputChoices();
    void refreshStatus();
    void refreshSystemStatus();

    // MIDI
    void openMidiSettings();
    void applyMidiBindings();
    void applyMidiOutput();

    // Show
    void openInitialShow();
    void showMenu();
    void askForNewShow();
    void chooseShowToOpen();
    void chooseFolderForCopy();
    void refreshShowStatus();
    void reportProblems();

    // Stage
    void setStageVisible (bool);
    void showStageSettingsMenu (juce::PopupMenu&);

    // Chain view
    void showSelectedBlock();
    void relayout();
    int layoutContent (int width, bool apply);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    AppSettings& settings;

    // Order matters: the engine must outlive the device manager that calls it,
    // and the session (which saves on close) must go before both.
    AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<ShowSession> session;
    system::TuningReport tuning;

    // Top bar
    juce::Label title, showName, saveState;
    juce::TextButton showButton { "Show" };
    juce::TextButton audioSettingsButton { "Audio settings..." };
    juce::TextButton midiSettingsButton { "MIDI..." };
    juce::TextButton stageButton { "Stage" }, tunerButton { "Tuner" }, panicButton { "PANIC" };

    std::unique_ptr<StageView> stageView;
    TunerDisplay editTuner;
    juce::TextButton muteButton { "MUTED" };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    // Editing
    std::unique_ptr<SetList> setList;
    std::unique_ptr<PatchList> patchList;
    std::unique_ptr<ChainStrip> chainStrip;
    std::unique_ptr<BlockPanel> blockPanel;
    juce::Label chainLabel, problemsLabel;
    Chain* shownChain = nullptr;
    int shownBlockIndex = -1;

    // Audio status
    juce::Label deviceStatus, latencyStatus, healthStatus, warning, systemStatus;
    juce::Label inputLabel { {}, "Guitar input" };
    juce::ComboBox inputChoice;
    juce::TextButton resetCountersButton { "Reset counters" };
    LevelMeter inputMeter { "Input" }, outputMeter { "Output" };

    // Open settings windows (closed before the things they edit go away).
    juce::Component::SafePointer<juce::DialogWindow> audioDialog, midiDialog;

    // MIDI state last given to the router.
    std::vector<MidiBinding> appliedBindings;
    juce::String openMidiOutput;
    bool midiOutputPresent = false;

    std::unique_ptr<juce::FileChooser> chooser;
    int frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace encore
