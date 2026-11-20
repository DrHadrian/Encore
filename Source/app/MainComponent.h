// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "platform/SystemTuning.h"
#include "session/PluginCatalog.h"
#include "session/ShowSession.h"
#include "ui/BlockPanel.h"
#include "ui/JackStage.h"
#include "ui/AmpBuilderView.h"
#include "session/AmpLibrary.h"
#include "ui/BottomRow.h"
#include "ui/RoutingView.h"
#include "ui/kit/Fader.h"
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
#include "ui/kit/StatusBar.h"
#include "ui/kit/HealthStatus.h"
#include "ui/kit/PaneTab.h"
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

    /** Settings — Audio | MIDI | Plugins (Settings... on the tab used last; the
        pre-show check's fixes, Routing and Find plugins... on their own tab). */
    void openSettings (SettingsWindow::Tab);
    void openSettingsOnLastTab();
    void closeSettings();
    bool isShowingSettings() const noexcept { return settingsWindow != nullptr; }
    std::optional<SettingsWindow::Tab> getShownSettingsTab() const   { return settingsWindow != nullptr ? std::optional (settingsWindow->getTab()) : std::nullopt; }

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

    // The bottom row (tests): the mixer view, the health status and its details.
    bool isMixerOpen() const noexcept { return mixerOpen; }

    // The amp builder (F8; tests).
    void setAmpBuilderVisible (bool);
    bool isShowingAmpBuilder() const noexcept { return ampBuilder != nullptr && ampBuilder->isVisible(); }
    AmpBuilderView* getAmpBuilder() noexcept { return ampBuilder.get(); }
    ChainStrip* getChainStrip() noexcept { return chainStrip.get(); }
    BlockPanel* getBlockPanel() noexcept { return blockPanel.get(); }
    void setMixerOpen (bool);
    kit::PaneTab& getMixerTab() noexcept { return mixerTab; }
    InputSummary& getInputSummary() noexcept { return inputSummary; }
    OutputsPane& getOutputsPane() noexcept { return outputsPane; }
    kit::HealthStatus& getHealthStatus() noexcept { return healthStatus; }
    bool isShowingHealthDetails() const noexcept { return healthDialog != nullptr; }
    HealthDetailsDialog* getHealthDetails() noexcept { return healthDialog.get(); }
    void openHealthDetails();
    void closeHealthDetails();
    void showInputPicker (juce::Component& anchor);

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

public:
    /** The Routing view (F7): shown or hidden; the Routing button follows. */
    void setRoutingVisible (bool);
    bool isShowingRouting() const noexcept { return routingView != nullptr && routingView->isVisible(); }
    RoutingView* getRoutingView() const noexcept { return routingView.get(); }

private:
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
    void showJackInspector();
    void relayout();
    void layoutToolbar (juce::Rectangle<int>);
    void layoutBottomRow (juce::Rectangle<int>);

    // Status
    void refreshStatusBar();
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
    kit::StatusBar statusBar { false };   // the toolbar's status bar (v3.2; was the LCD)
    juce::TextButton settingsButton { juce::String (juce::CharPointer_UTF8 ("Settings\xe2\x80\xa6")) };   // Audio | MIDI | Plugins, on the tab used last (v3.2)
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
    std::unique_ptr<JackStage> jackStage;   // the IN or OUT tile's stage
    juce::Label chainLabel, problemsLabel;
    juce::TextButton routingButton { "Routing" };   // centre header, F7
    std::unique_ptr<RoutingView> routingView;

    // The amp builder (design/AMP-BUILDER.md, F8): over the whole area under the toolbar.
    std::unique_ptr<AmpLibrary> ampLibrary;
    std::unique_ptr<AmpBuilderView> ampBuilder;
    int builderBlockIndex = -1;             // the amp block it builds for (-1: none)
    juce::String builderCaptureInfo;
    std::unique_ptr<juce::FileChooser> captureChooser;
    juce::TextButton patchPicker;
    juce::Label loadingLabel { {}, juce::String (juce::CharPointer_UTF8 ("(loading patches\xe2\x80\xa6)")) };
    juce::Rectangle<int> headerArrow;
    Chain* shownChain = nullptr;
    int shownBlockIndex = -1;

    // Bottom row (V3.2-CHANGES §3): the playing input, the outputs, the health status; the mixer view's tab.
    kit::Pane inputPane, metersPane;
    kit::HealthStatus healthStatus;
    kit::PaneTab mixerTab;
    bool mixerOpen = false;
    std::unique_ptr<HealthDetailsDialog> healthDialog;
    juce::StringArray healthProblems;   // what the health status counts (the warning line's, without plain info)
    juce::String healthDetail;          // "CPU 9% · 5.3 ms"
    void refreshHealthStatus();
    HealthReport healthReport();          // what the System health window shows
    void resetCounters();                 // the engine's counts, and the driver's dropouts from now
    int driverDropouts();                 // since the last reset (-1: the driver doesn't count them)
    int dropoutsAtReset = 0;
    juce::String limiterText { "idle" };   // the last limiter reading (taking it resets it)
    struct SystemCheck { juce::String text; bool fine = true; };
    std::vector<SystemCheck> systemChecks; // Windows: power, priority, throttling, the audio thread
    std::unique_ptr<Inspector> inspector;
    juce::Label deviceLine;   // not shown: the stage footer and Routing read it
    InputSummary inputSummary;
    int summaryInputSlot = -1;
    OutputsPane outputsPane;
    void refreshBottomRow();
    void refreshChainJacks();
    juce::Label healthLine, latencyLine, warningLine;   // not shown: the health details read them (full text in their tooltips)

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
