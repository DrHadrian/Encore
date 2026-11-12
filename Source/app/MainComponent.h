// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "platform/SystemTuning.h"
#include "session/PluginCatalog.h"
#include "session/ShowSession.h"
#include "ui/BlockPanel.h"
#include "ui/BottomRow.h"
#include "ui/ChainStrip.h"
#include "ui/Inspector.h"
#include "ui/MidiSettings.h"
#include "ui/SettingsWindow.h"
#include "session/RecoveryFile.h"
#include "ui/CueListEditor.h"
#include "ui/PatchList.h"
#include "ui/PluginWindow.h"
#include "ui/PreShowCheckView.h"
#include "ui/SetList.h"
#include "ui/StageView.h"
#include "ui/TunerDisplay.h"
#include "ui/kit/Lcd.h"
#include "ui/kit/MeterBar.h"
#include "ui/kit/NoticeBar.h"
#include "ui/kit/Pane.h"
#include "ui/kit/SegmentedControl.h"
#include "ui/kit/Toolbar.h"

namespace encore
{

/** The edit view (docs/design/SCREENS.md §4): a toolbar, a notice bar while a
    notice shows, the running order and patches on the left, the selected
    patch's chain and block in the centre, the inspector on the right, and
    the guitar input, meters and system health along the bottom.
    F5 (or "Stage") switches to the stage view, which covers the window.
*/
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    /** `deviceType` (tests): the only audio device type offered, instead of
        the system's drivers. */
    explicit MainComponent (AppSettings& settings, std::unique_ptr<juce::AudioIODeviceType> deviceType = nullptr);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Below this size the edit view scrolls (the stage view always fits the window). */
    juce::Point<int> getMinimumEditSize() const;

    /** The stage view fills the window instead of scrolling: the window
        re-lays out when it's switched. */
    std::function<void()> onViewChanged;
    bool isShowingStage() const noexcept { return stageView != nullptr && stageView->isVisible(); }

    /** Settings — Audio | MIDI (Audio..., MIDI..., the pre-show check's fixes). */
    void openSettings (SettingsWindow::Tab);
    void closeSettings();
    bool isShowingSettings() const noexcept { return settingsWindow != nullptr; }

    /** This computer's plugin catalog (Settings › Plugins). */
    PluginCatalog& getPluginCatalog() noexcept { return *plugins; }

    /** The first start (no plugin list yet): scan for plugins in the background. */
    void startFirstPluginScanIfNeeded();

    /** A plugin block's own window (the plugin's editor). */
    void openPluginWindow (PluginBlock&);
    bool isShowingPluginWindow (const juce::String& blockId) const { return pluginWindows.count (blockId) > 0; }

    /** The Show menu (the Show ▾ button opens it). */
    juce::PopupMenu buildShowMenu();

    /** The pre-show check (F9, Show → Pre-show check...). */
    void openPreShowCheck();
    void closePreShowCheck();
    bool isShowingPreShowCheck() const noexcept { return checkDialog != nullptr; }

private:
    // Audio
    void openAudioSettings();
    void refreshInputChoices();   // warns when no input channel is on
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
    void showPatchPicker();
    void reportProblems();

    // Robustness
    void recoverAfterUncleanExit();
    void updateRecoveryFile();
    void watchAudioDevice();
    void reconnectAudioDevice();
    PreShowFacts gatherPreShowFacts();
    void applyFix (const PreShowCheckResult&);

    // Routing (milestone 11): this computer's inputs and outputs.
    void setRouting (const RoutingConfig&);
    void setOutputMuted (const juce::String& outputName, bool shouldBeMuted);
    void ensureRoutingChannelsActive();
    void openRouting();
    juce::String lastRoutedDevice;   // the interface whose channels the routing last switched on
    void showNotice (const juce::String& text, int seconds, bool logged = false);

    // Cue list
    void setCueListVisible (bool);
    bool handleGoToKey (const juce::KeyPress&);

    // Stage
    void setStageVisible (bool);
    void showStageSettingsMenu (juce::PopupMenu&);

    // Chain view
    void showSelectedBlock();
    void relayout();
    void layoutToolbar (juce::Rectangle<int>);
    void layoutBottomRow (juce::Rectangle<int>);

    // Status
    void refreshLcd();
    void refreshWarningLine();
    void setWarning (const juce::String& text);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    AppSettings& settings;

    // Order matters: the engine must outlive the device manager that calls it,
    // and the session (which saves on close) must go before both.
    AudioEngine engine;
    juce::AudioDeviceManager deviceManager;
    std::shared_ptr<PluginCatalog> plugins;
    std::unique_ptr<ShowSession> session;
    system::TuningReport tuning;

    // Toolbar (SCREENS.md §4.1)
    class ShowTitle;
    kit::Toolbar toolbar;
    std::unique_ptr<ShowTitle> showTitle;
    juce::TextButton showButton { "Show" };
    kit::SegmentedControl modeSwitch;
    kit::Lcd lcd { false };
    juce::TextButton audioSettingsButton { "Audio..." };
    juce::TextButton midiSettingsButton { "MIDI..." };
    juce::TextButton tunerButton { "Tuner" }, panicButton { "PANIC" };
    juce::Label outLabel { {}, "Out" };
    kit::NoticeBar noticeBar { kit::NoticeBar::Kind::notice };

    std::unique_ptr<CueListEditor> cueEditor;
    juce::String goToNumber;
    juce::uint32 goToUntil = 0;

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
    juce::TextButton patchPicker;
    juce::Label loadingLabel { {}, "(loading patches...)" };
    juce::Rectangle<int> headerArrow;
    Chain* shownChain = nullptr;
    int shownBlockIndex = -1;

    // Bottom row (NEW-FEATURES-M10-M11 §B.4): the playing input, the outputs, system health.
    kit::Pane inputPane, metersPane, healthPane;
    std::unique_ptr<Inspector> inspector;
    juce::Label deviceLine;   // not shown: the stage footer and Routing read it
    InputSummary inputSummary;
    OutputsPane outputsPane;
    void refreshBottomRow();
    juce::Label healthTitle { {}, "System health" };
    juce::TextButton resetCountersButton { "Reset counters" };
    juce::Label healthLine, latencyLine, warningLine;   // full text in their tooltips

    // What the warning line shows (the most important first: lost, a device
    // or input problem, not ASIO, then the Windows / power line).
    juce::String warning, systemText, mutedOutputsText;
    bool systemProblem = false;
    double roundTripMs = 0.0, sampleRate = 0.0;
    juce::String stageDeviceLine, footswitchLine;   // the stage footer (SCREENS.md §3.4)

    // Open settings windows (closed before the things they edit go away).
    std::unique_ptr<PreShowCheckDialog> checkDialog;
    std::unique_ptr<SettingsWindow> settingsWindow;
    AudioSettingsView::Status audioStatus;

    // Plugin windows by block ID; closed before their plugin, reopened after a rebuild.
    void closePluginWindow (const juce::String& blockId, bool reopenWhenRebuilt);
    void syncPluginWindows();
    bool isPluginWindowActive() const;
    std::map<juce::String, std::unique_ptr<PluginWindow>> pluginWindows;
    std::map<juce::String, juce::Rectangle<int>> pluginWindowBounds;
    juce::StringArray pluginWindowsToReopen;
    juce::String crashedPluginNote;

    // Crash recovery: where the show is, kept on disk while running.
    std::unique_ptr<RecoveryFile> recovery;
    std::optional<RecoveryFile::State> uncleanExit;
    int recoveriesInARow = 0;
    juce::Time lastRecovery;

    // Audio device watchdog: callbacks must keep coming while a device is open.
    juce::uint32 lastCallbackCount = 0, lastCallbackTime = 0, nextReconnectAttempt = 0, lastGoodSetupTime = 0;
    bool deviceLost = false, haveGoodSetup = false, audioDialogWasOpen = false;
    int reconnectAttempts = 0;
    juce::AudioDeviceManager::AudioDeviceSetup lastGoodSetup;
    juce::String lastGoodType;

    juce::String notice;
    juce::uint32 noticeUntil = 0;

    // MIDI state last given to the router.
    std::vector<MidiBinding> appliedBindings;
    juce::String openMidiOutput;
    bool midiOutputPresent = false;

    std::unique_ptr<juce::FileChooser> chooser;
    int frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace encore
