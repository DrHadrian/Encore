// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/MainComponent.h"
#include "app/EventLog.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/core/Realtime.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

namespace
{
    using namespace theme;

    void styleLabel (juce::Label& label, const type::Style& style, juce::uint32 colourToken)
    {
        label.setFont (font (style));
        label.setColour (juce::Label::textColourId, paint::c (colourToken));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setBorderSize ({});
        label.setMinimumHorizontalScale (1.0f);
    }

    const juce::String& sep()
    {
        static const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));   // " · "
        return dot;
    }

    juce::String formatMs (int samples, double sampleRate)
    {
        return juce::String (1000.0 * samples / sampleRate, 1) + " ms";
    }

    /** Keyboard focus stays with the main window, so Space and the arrow keys
        always step the show. Otherwise a knob or the input menu clicked a
        moment earlier would take the arrow keys (and change the guitar input
        mid-show). The mouse still works on everything. */
    void keepKeysForTheShow (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (dynamic_cast<juce::TextEditor*> (child) == nullptr)
            {
                child->setWantsKeyboardFocus (false);
                child->setMouseClickGrabsKeyboardFocus (false);
            }

            keepKeysForTheShow (*child);
        }
    }

    juce::File defaultShowsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Encore").getChildFile ("Shows");
    }
}

//==============================================================================
/** The show name over the save state, in the toolbar (two lines, one tooltip:
    the show's folder). */
class MainComponent::ShowTitle final : public juce::Component,
                                       public juce::SettableTooltipClient
{
public:
    ShowTitle() { setInterceptsMouseClicks (true, false); }

    void set (const juce::String& newName, const juce::String& newState)
    {
        if (newName != name || newState != state)
        {
            name = newName;
            state = newState;
            width = (int) std::ceil (juce::jmax (textWidth (font (type::toolbarTitle), name), textWidth (font (type::saveState), state)));
            setTitle (name);
            repaint();
        }
    }

    int getIdealWidth() const noexcept { return width; }

    void paint (juce::Graphics& g) override
    {
        // 12.5 / 600 over 10.5, line height 1.15, the pair centred on the bar.
        const auto nameH = type::toolbarTitle.px * type::titleLineHeight, stateH = type::saveState.px * type::titleLineHeight;
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), nameH + stateH);
        paint::text (g, name, r.removeFromTop (nameH), type::toolbarTitle, colour::text);
        paint::text (g, state, r, type::saveState, colour::textMuted);
    }

private:
    juce::String name, state;
    int width = 0;
};

//==============================================================================
MainComponent::MainComponent (AppSettings& s, std::unique_ptr<juce::AudioIODeviceType> deviceType) : settings (s)
{
    // What DAWs do so Windows never slows the audio down (see SystemTuning.h).
    tuning = system::applyLiveProcessSettings();

    setWantsKeyboardFocus (true);

    // Toolbar (SCREENS.md §4.1)
    addAndMakeVisible (toolbar);
    showTitle = std::make_unique<ShowTitle>();
    toolbar.addAndMakeVisible (*showTitle);

    setButtonIcon (showButton, Icon::chevronDown, 9.0f, true);
    showButton.onClick = [this] { showMenu(); };
    toolbar.addAndMakeVisible (showButton);

    modeSwitch.setSegments ({ "Stage", "Edit", "Layout" });
    modeSwitch.setTooltips ({ "The performance screen (F5)", {}, "Move, resize, add and remove what this screen shows" });
    modeSwitch.setTitle ("Mode");
    modeSwitch.setSelected (1);
    modeSwitch.onClick = [this] (int index)
    {
        modeSwitch.setSelected (1);   // this switch lives in the edit view: Edit stays lit here

        if (index == 0)
            setStageVisible (true);
        else if (index == 2)
        {
            setStageVisible (true);
            stageView->setEditingLayout (true);
        }
    };
    toolbar.addAndMakeVisible (modeSwitch);

    toolbar.addAndMakeVisible (lcd);

    tunerButton.setTooltip ("Tuner (T)");
    tunerButton.setClickingTogglesState (false);
    tunerButton.onClick = [this] { engine.setTunerOpen (! engine.isTunerOpen()); };
    toolbar.addAndMakeVisible (tunerButton);

    midiSettingsButton.onClick = [this] { openMidiSettings(); };
    midiSettingsButton.setTooltip ("Foot controller, expression pedals, MIDI learn and MIDI out");
    toolbar.addAndMakeVisible (midiSettingsButton);

    audioSettingsButton.onClick = [this] { openAudioSettings(); };
    audioSettingsButton.setTooltip ("Audio settings: interface, driver, buffer and channels");
    toolbar.addAndMakeVisible (audioSettingsButton);

    styleLabel (outLabel, type::label, colour::textTertiary);
    toolbar.addAndMakeVisible (outLabel);

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, size::valueFieldW, size::fieldHeight);
    volume.textFromValueFunction = [] (double value)
    {
        // A real minus sign, as everywhere in the displays: "−12.0" (the
        // slider adds the " dB" suffix).
        const auto rounded = std::round (value * 10.0) / 10.0;
        return (rounded < 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())
               + juce::String (std::abs (rounded), 1);
    };
    volume.valueFromTextFunction = [] (const juce::String& text)
    {
        // Typed values: "-6", "−6", "-6 dB" all work.
        return text.replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-").retainCharacters ("-+.0123456789").getDoubleValue();
    };
    volume.setTooltip ("Master volume");
    volume.setTitle ("Master volume");
    volume.setValue (settings.getMasterGainDecibels(), juce::dontSendNotification);
    volume.updateText();
    engine.setMasterGainDecibels ((float) volume.getValue());
    volume.onValueChange = [this]
    {
        engine.setMasterGainDecibels ((float) volume.getValue());
        settings.setMasterGainDecibels ((float) volume.getValue());
    };
    toolbar.addAndMakeVisible (volume);

    setVariant (panicButton, Variant::danger);
    panicButton.setTooltip ("Silence everything at once and clear all tails (see Show > Stage settings)");
    panicButton.onClick = [this] { engine.panic(); };
    toolbar.addAndMakeVisible (panicButton);

    // Encore always starts muted: a laptop booting next to a hot PA must not
    // blast. Unmuting is one deliberate click (a footswitch, from milestone 6).
    setVariant (muteButton, Variant::muted);
    muteButton.setClickingTogglesState (true);
    muteButton.setToggleState (true, juce::dontSendNotification);
    muteButton.onClick = [this]
    {
        const auto isMuted = muteButton.getToggleState();
        engine.setMuted (isMuted);
        muteButton.setButtonText (isMuted ? "MUTED" : "LIVE");
    };
    toolbar.addAndMakeVisible (muteButton);

    editTuner.onClick = [this] { engine.setTunerOpen (false); };
    addChildComponent (editTuner);

    // Notice bar (SCREENS.md §4.2): shown while a notice is.
    addChildComponent (noticeBar);

    // Bottom row (SCREENS.md §4.4)
    for (auto* pane : { &inputPane, &metersPane, &healthPane })
        addAndMakeVisible (pane);

    styleLabel (inputLabel, type::label, colour::textTertiary);
    inputPane.addAndMakeVisible (inputLabel);

    setVariant (inputChoice, Variant::field);
    inputChoice.setTitle ("Guitar input");
    inputChoice.onChange = [this]
    {
        // The item's id is the interface's channel (1-based): the first input's channel.
        const auto channel = inputChoice.getSelectedId();

        if (channel > 0)
        {
            auto routing = engine.getRouting();
            routing.inputs.front().channel = channel;
            setRouting (routing);
        }
    };
    inputPane.addAndMakeVisible (inputChoice);

    styleLabel (deviceLine, type::caption, colour::textMuted);
    inputPane.addAndMakeVisible (deviceLine);

    metersPane.addAndMakeVisible (inputMeter);
    metersPane.addAndMakeVisible (outputMeter);
    metersPane.addAndMakeVisible (meterScale);

    styleLabel (healthTitle, type::healthTitle, colour::textTertiary);
    healthPane.addAndMakeVisible (healthTitle);

    setVariant (resetCountersButton, Variant::small);
    resetCountersButton.setTooltip ("Start the overrun, dropout and clip counts again from zero");
    resetCountersButton.onClick = [this] { engine.resetCounters(); };
    healthPane.addAndMakeVisible (resetCountersButton);

    for (auto* line : { &healthLine, &latencyLine, &warningLine })
    {
        styleLabel (*line, type::caption, colour::textSecondary);
        healthPane.addAndMakeVisible (line);
    }

    warningLine.setJustificationType (juce::Justification::topLeft);

    styleLabel (chainLabel, type::headerLine, colour::text);
    addAndMakeVisible (chainLabel);

    setVariant (patchPicker, Variant::field);
    patchPicker.onClick = [this] { showPatchPicker(); };
    addChildComponent (patchPicker);

    styleLabel (loadingLabel, type::label, colour::textMuted);
    addChildComponent (loadingLabel);

    styleLabel (problemsLabel, type::label, colour::amber);
    problemsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (problemsLabel);

    // Open the audio device: the saved one if possible, else a sensible default.
    // Prefer ASIO on first run because it gives the lowest, most stable latency.
    if (deviceType != nullptr)
        deviceManager.addAudioDeviceType (std::move (deviceType));   // the system's types are then never scanned

    auto savedState = settings.getAudioDeviceState();
    auto error = deviceManager.initialise (2, 2, savedState.get(), true);
    eventlog::write ("Audio: " + (error.isEmpty() ? deviceManager.getCurrentAudioDeviceType() + " / "
                                                        + (deviceManager.getCurrentAudioDevice() != nullptr
                                                               ? deviceManager.getCurrentAudioDevice()->getName() : juce::String ("none"))
                                                  : "problem: " + error));

    if (savedState == nullptr)
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
            {
                deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }

    if (error.isNotEmpty())
        setWarning ("Audio device problem: " + error);

    // This computer's inputs and outputs. The first time (before routing), the
    // guitar input was a place among the switched-on inputs: it becomes the
    // interface channel it pointed at.
    auto routing = settings.getRouting();

    if (! settings.hasRouting())
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            const auto active = device->getActiveInputChannels();
            const auto wanted = juce::jmax (0, settings.getGuitarInputChannel());

            for (int ch = 0, index = 0; ch <= active.getHighestBit(); ++ch)
                if (active[ch] && index++ == wanted)
                {
                    routing.inputs.front().channel = ch + 1;
                    break;
                }

            settings.setRouting (routing);
        }

    engine.setRouting (routing);
    deviceManager.addAudioCallback (&engine);

    // MIDI: every enabled input feeds the router (the device manager reopens
    // inputs that were enabled last time, and ones plugged back in).
    deviceManager.addMidiInputDeviceCallback ({}, &engine.getMidiRouter());
    deviceManager.addChangeListener (this);

    if (auto* device = deviceManager.getCurrentAudioDevice())
        lastRoutedDevice = device->getName();

    ensureRoutingChannelsActive();

    // Did the last run end cleanly? If not, go back to where it was.
    recovery = std::make_unique<RecoveryFile> (settings.getFolder().getChildFile ("recovery.json"));
    uncleanExit = recovery->readUncleanExit();

    // Plugins this computer has (Settings > Plugins); scanned in separate processes.
    plugins = std::make_shared<PluginCatalog> (settings.getFolder().getChildFile ("plugins.xml"),
                                               juce::File::getSpecialLocation (juce::File::currentExecutableFile));

    // A plugin's window closes before its plugin does (a rebuilt patch reopens it).
    PluginBlock::setBeforeClosing ([this] (PluginBlock& block) { closePluginWindow (block.getModelId(), true); });

    // The show: with the device running, patches build for the right rate.
    session = std::make_unique<ShowSession> (engine);
    session->setPluginCatalog (plugins);
    session->addChangeListener (this);

    // Crashed inside a plugin last time? That block starts switched off.
    if (const auto crashFile = settings.getFolder().getChildFile ("plugin-crash.txt"); crashFile.existsAsFile())
    {
        const auto crashed = PluginBlock::parseCrashLabels (crashFile.loadFileAsString());
        crashFile.deleteFile();

        if (uncleanExit && ! crashed.empty())
        {
            juce::StringArray blockIds, names;

            for (const auto& [blockId, pluginName] : crashed)
            {
                blockIds.add (blockId);
                names.addIfNotAlreadyThere (pluginName);
            }

            session->setCrashedPlugins (blockIds);
            crashedPluginNote = ". " + names.joinIntoString (", ") + (names.size() == 1 ? " was" : " were")
                                + " the cause: switched off for now";
            eventlog::write ("Crashed inside the plugin " + names.joinIntoString (", ") + ": it starts switched off");
        }
    }

    setList = std::make_unique<SetList> (*session);
    addAndMakeVisible (*setList);

    patchList = std::make_unique<PatchList> (*session);
    addAndMakeVisible (*patchList);

    inspector = std::make_unique<Inspector> (*session);
    inspector->onEdited = [this] { session->markEdited(); };
    inspector->onOpenPluginWindow = [this]
    {
        if (auto* chain = engine.getChain(); chain != nullptr && juce::isPositiveAndBelow (shownBlockIndex, (int) chain->getBlocks().size()))
            if (auto* plugin = dynamic_cast<PluginBlock*> (chain->getBlocks()[(size_t) shownBlockIndex].get()))
                openPluginWindow (*plugin);
    };
    addAndMakeVisible (*inspector);

    chainStrip = std::make_unique<ChainStrip> (*session);
    chainStrip->onSelectionChanged = [this] (int) { showSelectedBlock(); };
    chainStrip->onFindPlugins = [this] { openSettings (SettingsWindow::Tab::plugins); };
    addAndMakeVisible (*chainStrip);

    openInitialShow();

    cueEditor = std::make_unique<CueListEditor> (*session);
    cueEditor->onClose = [this] { setCueListVisible (false); };
    addChildComponent (*cueEditor);
    setList->onExpand = [this] { setCueListVisible (true); };

    stageView = std::make_unique<StageView> (engine, *session);
    stageView->setOpaque (true);
    stageView->onLeave = [this] { setStageVisible (false); };
    stageView->setLayoutInspectorShown (settings.getLayoutInspectorShown());
    stageView->onLayoutInspectorChanged = [this] (bool shown) { settings.setLayoutInspectorShown (shown); };
    addChildComponent (*stageView);

    recoverAfterUncleanExit();
    applyMidiBindings();
    applyMidiOutput();
    keepKeysForTheShow (*this);

    refreshInputChoices();
    refreshStatus();
    refreshSystemStatus();
    refreshShowStatus();

    setSize (1100, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();

    // Plugin windows close before their plugins (which go with the engine).
    pluginWindows.clear();
    PluginBlock::setBeforeClosing (nullptr);

    // Settings windows hold references to the engine, devices and session.
    settingsWindow = nullptr;
    checkDialog = nullptr;

    stageView = nullptr;
    cueEditor = nullptr;
    blockPanel = nullptr;
    inspector = nullptr;
    chainStrip = nullptr;
    patchList = nullptr;
    setList = nullptr;

    if (session != nullptr)
    {
        session->removeChangeListener (this);
        session = nullptr;   // saves unsaved edits
    }

    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &engine.getMidiRouter());
    engine.getMidiRouter().setOutput (nullptr);
    deviceManager.removeAudioCallback (&engine);
    settings.save();

    recovery->markCleanExit();
}

//==============================================================================
void MainComponent::openInitialShow()
{
    // After a crash, the show that was playing (even if another was opened since).
    const auto last = uncleanExit && uncleanExit->showFolder.isNotEmpty() ? juce::File (uncleanExit->showFolder)
                                                                          : settings.getLastShowFolder();

    if (last != juce::File() && last.isDirectory() && session->openShow (last).wasOk())
        return;

    // First start (or the last show is gone): make a show in Documents\Encore\Shows,
    // starting from the amp and cab used last, if any.
    const auto folder = ShowFolder::suggestFolder (defaultShowsFolder(), "My Show");
    std::map<juce::String, juce::File> starters;

    for (auto type : { "nam", "cab" })
        if (const auto f = settings.getLastFile (type); f.existsAsFile())
            starters[type] = f;

    if (const auto r = session->newShow (folder, "My Show", starters); r.failed())
        setWarning ("Couldn't create a show: " + r.getErrorMessage());
    else
        settings.setLastShowFolder (folder);
}

void MainComponent::showMenu()
{
    buildShowMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&showButton));
}

juce::PopupMenu MainComponent::buildShowMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("New show...", [this] { askForNewShow(); });
    menu.addItem ("Open show...", [this] { chooseShowToOpen(); });
    menu.addSeparator();
    menu.addItem ("Save now (Ctrl+S)", [this] { session->save(); reportProblems(); });
    menu.addItem ("Save a copy to... (USB stick, backup)", [this] { chooseFolderForCopy(); });
    menu.addSeparator();
    menu.addItem ("Show folder in Explorer", session->getFolder().isValid(), false,
                  [this] { session->getFolder().getShowFile().revealToUser(); });
    juce::PopupMenu showType;

    for (auto type : { ShowTemplate::band, ShowTemplate::musical, ShowTemplate::worship, ShowTemplate::solo })
        showType.addItem (describe (type) + "  (" + wordsFor (type).list + ")", true, session->getShow().showTemplate == type,
                          [this, type] { session->setShowTemplate (type); });

    menu.addSubMenu ("Kind of show", showType);
    menu.addSeparator();
    menu.addItem ("Pre-show check... (F9)", [this] { openPreShowCheck(); });
    menu.addItem ("Open log folder", [] { eventlog::getLogsFolder().startAsProcess(); });
    menu.addSeparator();

    juce::PopupMenu stage;
    showStageSettingsMenu (stage);
    menu.addSubMenu ("Stage settings (this show)", stage);
    menu.addSeparator();
    menu.addItem ("Open backups folder", session->getFolder().isValid(), false,
                  [this] { session->getFolder().getBackupsFolder().startAsProcess(); });

    return menu;
}

void MainComponent::askForNewShow()
{
    auto* window = new juce::AlertWindow ("New show", "Name of the show (band, production, set...):",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", "New show");
    window->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        const auto name = window->getTextEditorContents ("name").trim();
        const auto folder = ShowFolder::suggestFolder (defaultShowsFolder(), name);

        if (const auto r = safeThis->session->newShow (folder, name); r.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't create the show", r.getErrorMessage());
        else
            safeThis->settings.setLastShowFolder (folder);
    }), true);
}

void MainComponent::chooseShowToOpen()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a show (its .encore folder or show.json)",
                                                   defaultShowsFolder(), "*.encore;show.json");
    juce::Component::SafePointer<MainComponent> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              const auto picked = fc.getResult();

                              if (safeThis == nullptr || picked == juce::File())
                                  return;

                              if (const auto r = safeThis->session->openShow (picked); r.failed())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Couldn't open the show", r.getErrorMessage());
                              else
                                  safeThis->settings.setLastShowFolder (safeThis->session->getFolder().getFolder());
                          });
}

void MainComponent::chooseFolderForCopy()
{
    chooser = std::make_unique<juce::FileChooser> ("Copy the show into...",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    juce::Component::SafePointer<MainComponent> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              const auto parent = fc.getResult();

                              if (safeThis == nullptr || ! parent.isDirectory())
                                  return;

                              juce::File copy;
                              const auto r = safeThis->session->saveCopyTo (parent, copy);
                              juce::AlertWindow::showMessageBoxAsync (
                                  r.wasOk() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                  r.wasOk() ? "Show copied" : "Couldn't copy the show",
                                  r.wasOk() ? "The complete show is in\n" + copy.getFullPathName()
                                                + "\n\nOpen it on any computer with Show > Open show."
                                            : r.getErrorMessage());
                          });
}

void MainComponent::refreshShowStatus()
{
    const auto& show = session->getShow();
    const auto oldTitleWidth = showTitle->getIdealWidth();
    showTitle->set (show.name, session->hasUnsavedChanges() ? juce::String ("Unsaved changes (autosaves)")
                                                            : "Saved " + session->getLastSaveTime().formatted ("%H:%M:%S"));
    showTitle->setTooltip (session->getFolder().getFolder().getFullPathName());

    if (showTitle->getIdealWidth() != oldTitleWidth)
        layoutToolbar (toolbar.getLocalBounds());

    refreshLcd();

    // "Cue 3 of 12: Solo  →  [Patch: Lead ▾]" when playing from the set list,
    // else the patch name (SCREENS.md §4.3, the centre header line).
    juce::String text;
    const auto cues = session->getCues();
    const auto cue = session->getCurrentCue();
    const auto playing = juce::isPositiveAndBelow (cue, (int) cues.size());
    const auto* patch = show.findPatch (session->getActivePatchId());

    if (playing)
        text << "Cue " << (cue + 1) << " of " << (int) cues.size()
             << (cues[(size_t) cue]->name.isNotEmpty() ? ": " + cues[(size_t) cue]->name : juce::String());
    else if (patch != nullptr)
        text << patch->name;

    const auto pickerText = "Patch: " + (patch != nullptr ? patch->name : juce::String ("(none)"));
    const auto changed = text != chainLabel.getText() || playing != patchPicker.isVisible()
                         || pickerText != patchPicker.getButtonText() || session->isBuilding() != loadingLabel.isVisible();

    chainLabel.setText (text, juce::dontSendNotification);
    patchPicker.setButtonText (pickerText);
    patchPicker.setTooltip ("The patch this " + session->getWords().item + " plays");
    patchPicker.setVisible (playing);
    loadingLabel.setVisible (session->isBuilding());

    if (changed)
        resized();
}

void MainComponent::showPatchPicker()
{
    const auto cue = session->getCurrentCue();
    const auto cues = session->getCues();

    if (! juce::isPositiveAndBelow (cue, (int) cues.size()))
        return;

    const auto& item = *cues[(size_t) cue];
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::PopupMenu menu;

    for (auto& p : session->getShow().patches)
        menu.addItem (p.name, true, ! item.uniquePatch && p.id == item.patchId, [safeThis, cue, id = p.id]
        {
            if (safeThis != nullptr)
                safeThis->session->setCuePatch (cue, id);
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patchPicker));
}

void MainComponent::reportProblems()
{
    const auto problems = session->takeProblems();

    if (! problems.isEmpty())
    {
        problemsLabel.setText (problems[problems.size() - 1]
                                   + (problems.size() > 1 ? "  (+" + juce::String (problems.size() - 1) + " more)" : juce::String()),
                               juce::dontSendNotification);
        problemsLabel.setTooltip (problems.joinIntoString ("\n"));
    }
}

//==============================================================================
void MainComponent::showSelectedBlock()
{
    const auto index = chainStrip->getSelectedIndex();
    auto* chain = engine.getChain();

    if (chain == shownChain && index == shownBlockIndex && blockPanel != nullptr)
        return;

    shownChain = chain;
    shownBlockIndex = index;
    blockPanel = nullptr;
    inspector->setBlock (chain, index);

    if (chain != nullptr && juce::isPositiveAndBelow (index, (int) chain->getBlocks().size()))
    {
        auto& block = *chain->getBlocks()[(size_t) index];
        blockPanel = std::make_unique<BlockPanel> (block);

        if (auto* gate = dynamic_cast<NoiseGateBlock*> (&block))
            blockPanel->setStatusLight ("open", [gate] { return gate->isOpen(); });

        blockPanel->getBrowseFolder = [this] { return settings.getLastBrowseFolder(); };
        blockPanel->onFileLoaded = [this, &block] (const juce::File& file)
        {
            settings.setLastFile (block.getTypeId(), file);
            settings.setLastBrowseFolder (file.getParentDirectory());
            refreshStatus();   // latency may have changed
        };
        blockPanel->onEdited = [this] { session->markEdited(); };

        if (auto* plugin = dynamic_cast<PluginBlock*> (&block))
        {
            blockPanel->onOpenPluginWindow = [this, plugin] { openPluginWindow (*plugin); };
            blockPanel->onTurnPluginBackOn = [this, index] { session->turnPluginBackOn (index); };
        }

        // The amp with a cab right after it: the cab stands beside it (a click selects it).
        if (block.getTypeId() == NamAmpBlock::typeId && index + 1 < (int) chain->getBlocks().size()
            && chain->getBlocks()[(size_t) index + 1]->getTypeId() == CabIrBlock::typeId)
        {
            blockPanel->setCabBeside (chain->getBlocks()[(size_t) index + 1].get());
            blockPanel->onCabClicked = [this, index]
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), index]
                {
                    if (safe != nullptr)
                        safe->chainStrip->selectBlock (index + 1);
                });
            };
        }
        blockPanel->getExpressionPedal = [this, index] (const juce::String& id) { return session->getExpressionPedal (index, id); };
        blockPanel->onExpressionChosen = [this, index] (const juce::String& id, int pedal)
        {
            // Rebuilds the patch, which replaces this panel: do it after the menu callback.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), index, id, pedal]
            {
                if (safe != nullptr)
                    safe->session->setExpression (index, id, pedal);
            });
        };

        // Behind the stage view: the panel is rebuilt whenever the patch
        // changes, also while the stage view is showing.
        const auto behindStage = cueEditor != nullptr ? getIndexOfChildComponent (cueEditor.get()) : -1;
        addAndMakeVisible (*blockPanel, behindStage);
        keepKeysForTheShow (*blockPanel);
    }

    relayout();
}

void MainComponent::relayout()
{
    // Inside a scrolling viewport: let it adjust our height first...
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        viewport->resized();

    // ...then always lay out: if our size didn't change, JUCE won't call
    // resized() by itself, and a newly added panel would stay at zero size.
    resized();
}

//==============================================================================
void MainComponent::recoverAfterUncleanExit()
{
    if (! uncleanExit)
        return;

    const auto now = juce::Time::getCurrentTime();
    const auto goLive = RecoveryFile::shouldGoLiveAutomatically (*uncleanExit, now);

    recoveriesInARow = uncleanExit->lastRecovery.toMilliseconds() > 0 && now - uncleanExit->lastRecovery < juce::RelativeTime::minutes (10)
                           ? uncleanExit->recoveriesInARow + 1 : 1;
    lastRecovery = now;

    const auto cues = session->getCues();
    juce::String where;

    if (juce::isPositiveAndBelow (uncleanExit->cue, (int) cues.size()))
    {
        session->goToCue (uncleanExit->cue);
        const auto& cue = *cues[(size_t) uncleanExit->cue];
        where = " at cue " + (cue.number.isNotEmpty() ? cue.number + " " : juce::String()) + cue.name;
    }

    engine.setMuted (! goLive);

    if (uncleanExit->stageView)
        setStageVisible (true);

    eventlog::write ("Recovered after an unclean exit" + where + (goLive ? ", live" : ", muted (repeated crashes)"));
    showNotice ("Encore restarted after an unexpected exit - back" + where + crashedPluginNote
                    + (goLive ? juce::String() : juce::String (". MUTED: it happened again soon after, check before unmuting.")),
                30, true);
}

void MainComponent::updateRecoveryFile()
{
    RecoveryFile::State state;
    state.showFolder = session->getFolder().isValid() ? session->getFolder().getFolder().getFullPathName() : juce::String();
    state.cue = session->getCurrentCue();
    state.live = ! engine.isMuted();
    state.stageView = isShowingStage();
    state.recoveriesInARow = recoveriesInARow;
    state.lastRecovery = lastRecovery;
    recovery->update (state);   // only writes when something changed
}

void MainComponent::showNotice (const juce::String& text, int seconds, bool logged)
{
    notice = text;
    noticeUntil = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000u;
    noticeBar.setText (text, logged ? "shows for " + juce::String (seconds) + " s" + sep() + "written to the event log" : juce::String());

    const auto show = text.isNotEmpty() && seconds > 0;

    if (show != noticeBar.isVisible())
    {
        noticeBar.setVisible (show);
        resized();
    }
}

void MainComponent::setWarning (const juce::String& text)
{
    if (text != warning)
    {
        warning = text;
        refreshWarningLine();
    }
}

void MainComponent::refreshWarningLine()
{
    // One line, the most important first; the tooltip has everything.
    juce::StringArray all;
    juce::String shown;
    auto colourToken = colour::textSecondary;

    if (deviceLost)
    {
        shown = "AUDIO INTERFACE LOST - reconnecting to " + lastGoodSetup.outputDeviceName + "... (staying on the cue; check the cable)";
        colourToken = colour::danger;
        all.add (shown);
    }

    const auto warn = juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa0 "));   // "⚠ "

    if (warning.isNotEmpty())
    {
        all.add (warning);

        if (shown.isEmpty())
        {
            shown = warning.startsWith ("Not using ASIO")
                        ? warn + "Not using ASIO" + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 ")) + "pick an ASIO driver for the stage"
                        : warn + warning;
            colourToken = colour::amber;
        }
    }

    if (systemText.isNotEmpty())
    {
        all.add (systemText);

        if (shown.isEmpty())
        {
            shown = systemProblem ? warn + systemText : systemText;
            colourToken = systemProblem ? colour::amber : colour::textSecondary;
        }
    }

    warningLine.setText (shown, juce::dontSendNotification);
    warningLine.setTooltip (all.joinIntoString ("\n"));
    warningLine.setColour (juce::Label::textColourId, paint::c (colourToken));
}

void MainComponent::watchAudioDevice()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto count = engine.getCallbackCount();
    auto* device = deviceManager.getCurrentAudioDevice();

    // Changing settings in the dialog may close the device on purpose.
    const auto dialogOpen = settingsWindow != nullptr;

    if (audioDialogWasOpen && ! dialogOpen)
        haveGoodSetup = device != nullptr && device->isOpen();

    audioDialogWasOpen = dialogOpen;

    if (count != lastCallbackCount)
    {
        lastCallbackCount = count;
        lastCallbackTime = now;

        if (deviceLost)
        {
            deviceLost = false;
            eventlog::write ("Audio interface back after " + juce::String (reconnectAttempts) + " attempt(s)");
            showNotice ("Audio interface reconnected", 10, true);
            refreshStatus();
        }

        // Remember what works, to reopen exactly that.
        if (now - lastGoodSetupTime > 2000)
        {
            lastGoodSetupTime = now;
            lastGoodSetup = deviceManager.getAudioDeviceSetup();
            lastGoodType = deviceManager.getCurrentAudioDeviceType();
            haveGoodSetup = true;
        }

        return;
    }

    if (! haveGoodSetup || dialogOpen)
        return;

    const auto stalled = device != nullptr && device->isOpen() && now - lastCallbackTime > 500;
    const auto gone = device == nullptr || ! device->isOpen();

    if (! deviceLost && (stalled || gone))
    {
        deviceLost = true;
        reconnectAttempts = 0;
        nextReconnectAttempt = now + 200;
        eventlog::write (juce::String ("Audio interface ") + (stalled ? "stopped responding" : "disappeared") + ": "
                         + lastGoodType + " / " + lastGoodSetup.outputDeviceName);

        if (device != nullptr)
            deviceManager.closeAudioDevice();

        refreshStatus();
    }

    if (deviceLost && now >= nextReconnectAttempt)
    {
        nextReconnectAttempt = now + 1000;
        reconnectAudioDevice();
    }
}

void MainComponent::reconnectAudioDevice()
{
    ++reconnectAttempts;

    if (deviceManager.getCurrentAudioDeviceType() != lastGoodType)
        deviceManager.setCurrentAudioDeviceType (lastGoodType, true);

    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();

        // Only try when the interface is actually back (opening a missing
        // ASIO driver can take a while).
        if (! type->getDeviceNames (false).contains (lastGoodSetup.outputDeviceName))
            return;
    }

    const auto error = deviceManager.setAudioDeviceSetup (lastGoodSetup, true);

    if (error.isNotEmpty() && (reconnectAttempts == 1 || reconnectAttempts % 30 == 0))
        eventlog::write ("Reconnect attempt " + juce::String (reconnectAttempts) + ": " + error);
}

void MainComponent::openPreShowCheck()
{
    // Over the window (SCREENS.md §8); its fixes open the settings on top of it.
    if (checkDialog != nullptr)
        return;

    checkDialog = std::make_unique<PreShowCheckDialog> (
        [this] { return gatherPreShowFacts(); },
        [this] (const PreShowCheckResult& r) { applyFix (r); },
        [this]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe != nullptr)
                    safe->closePreShowCheck();
            });
        });

    auto& overlay = checkDialog->getOverlay();
    addAndMakeVisible (overlay);
    overlay.setBounds (getLocalBounds());
    overlay.enterModalState (isShowing());

    if (settingsWindow != nullptr)
        settingsWindow->getOverlay().toFront (isShowing());
}

void MainComponent::closePreShowCheck()
{
    if (checkDialog == nullptr)
        return;

    checkDialog->getOverlay().exitModalState (0);
    checkDialog = nullptr;

    if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::applyFix (const PreShowCheckResult& result)
{
    using Fix = PreShowCheckResult::Fix;

    // The dialogs open on top of the check window; it stays open behind.
    switch (result.fix)
    {
        case Fix::routing:        closePreShowCheck(); openRouting(); break;
        case Fix::unmuteOutput:   setOutputMuted (result.target, false); break;
        case Fix::audioSettings:  openAudioSettings(); break;
        case Fix::midiSettings:   openMidiSettings(); break;
        case Fix::pluginSettings: openSettings (SettingsWindow::Tab::plugins); break;
        case Fix::save:           session->save(); reportProblems(); break;
        case Fix::resetCounters:  engine.resetCounters(); break;
        case Fix::unmute:         engine.setMuted (false); break;
        case Fix::none:           break;
    }
}

PreShowFacts MainComponent::gatherPreShowFacts()
{
    PreShowFacts f;
    auto* device = deviceManager.getCurrentAudioDevice();

    f.reconnecting = deviceLost;
    f.deviceOpen = device != nullptr && device->isOpen();
    f.deviceType = deviceManager.getCurrentAudioDeviceType();
    f.deviceName = deviceLost ? lastGoodSetup.outputDeviceName : (device != nullptr ? device->getName() : juce::String());

    if (f.deviceOpen)
    {
        f.bufferSize = device->getCurrentBufferSizeSamples();
        f.sampleRate = device->getCurrentSampleRate();
        f.latencyMs = f.sampleRate > 0.0 ? (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + f.bufferSize)
                                               * 1000.0 / f.sampleRate
                                         : 0.0;
        f.driverDropouts = device->getXRunCount();
    }

    f.callbacksRunning = juce::Time::getMillisecondCounter() - lastCallbackTime < 500;
    f.cpuLoad = engine.getCpuLoad();
    f.overruns = engine.getOverrunCount();
    f.badBuffers = engine.getPlayer().getBadOutputCount() + engine.getPlayer().getBadInputCount();
    f.muted = engine.isMuted();

   #if JUCE_WINDOWS
    const auto power = system::getPowerSource();
    f.onBattery = power == system::PowerSource::battery;
    f.powerUnknown = power == system::PowerSource::unknown;
    f.highPriority = tuning.highPriority;
    f.throttlingOff = tuning.powerThrottlingDisabled;
   #else
    f.powerUnknown = true;
   #endif

    const auto& folder = session->getFolder();
    f.showHasFolder = folder.isValid();
    f.showSaved = ! session->hasUnsavedChanges();
    f.freeDiskBytes = folder.isValid() ? folder.getFolder().getBytesFreeOnVolume() : -1;
    f.numCues = (int) session->getCues().size();
    f.missingFiles = findMissingFiles (session->getShow(), folder);
    f.cuesWithoutPatch = findCuesWithoutPatch (session->getShow());
    f.pluginProblems = session->getPluginProblems();

    // MIDI inputs the user enabled (remembered even while unplugged) vs. present.
    if (const auto state = deviceManager.createStateXml())
        for (auto* input : state->getChildWithTagNameIterator ("MIDIINPUT"))
            f.midiInputsEnabled.addIfNotAlreadyThere (input->getStringAttribute ("name"));

    for (const auto& input : juce::MidiInput::getAvailableDevices())
        f.midiInputsConnected.add (input.name);

    for (const auto& binding : appliedBindings)
        f.hasNextCueMapping = f.hasNextCueMapping || binding.action == MidiAction::nextCue;

    for (const auto* cue : session->getCues())
        f.cuesSendMidi = f.cuesSendMidi || ! cue->midiOut.empty();

    f.midiOutputConnected = midiOutputPresent;

    f.routing = engine.getRouting();
    f.patchRouting = describePatchRouting (session->getShow());
    f.unalignedPatches = session->getUnalignedPatches();

    if (f.deviceOpen)
    {
        f.deviceInputChannels = engine.getDeviceInputChannels();
        f.deviceOutputChannels = engine.getDeviceOutputChannels();
        f.switchedOnInputs = f.switchedOnOutputs = 0;

        for (int ch = 1; ch <= routing::maxChannel; ++ch)
        {
            if (engine.isInputChannelUsable (ch))  f.switchedOnInputs  |= (juce::uint64) 1 << (ch - 1);
            if (engine.isOutputChannelUsable (ch)) f.switchedOnOutputs |= (juce::uint64) 1 << (ch - 1);
        }
    }

    return f;
}

//==============================================================================
void MainComponent::setRouting (const RoutingConfig& routing)
{
    engine.setRouting (routing);
    settings.setRouting (engine.getRouting());
    session->refreshRouting();   // inputs and sends by name, to the new slots
    ensureRoutingChannelsActive();
    refreshInputChoices();
}

void MainComponent::setOutputMuted (const juce::String& outputName, bool shouldBeMuted)
{
    const auto slot = engine.getOutputSlot (outputName);

    if (slot < 0)
        return;

    engine.setOutputMuted (slot, shouldBeMuted);

    if (engine.syncOutputMutes())
        settings.setRouting (engine.getRouting());
}

void MainComponent::ensureRoutingChannelsActive()
{
    // The interface channels the routing uses must be switched on, or they'd
    // stay silent; switch on the ones the interface has (never off).
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || deviceLost)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto numIns = device->getInputChannelNames().size();
    const auto numOuts = device->getOutputChannelNames().size();
    auto ins = device->getActiveInputChannels();
    auto outs = device->getActiveOutputChannels();
    bool changes = false;

    for (const auto& in : engine.getRouting().inputs)
        if (in.channel <= numIns && ! ins[in.channel - 1])
        {
            ins.setBit (in.channel - 1);
            changes = true;
        }

    for (const auto& out : engine.getRouting().outputs)
        for (auto ch : out.channels)
            if (ch <= numOuts && ! outs[ch - 1])
            {
                outs.setBit (ch - 1);
                changes = true;
            }

    if (! changes)
        return;

    setup.inputChannels = ins;
    setup.outputChannels = outs;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;

    if (const auto error = deviceManager.setAudioDeviceSetup (setup, true); error.isNotEmpty())
        eventlog::write ("Couldn't switch on the channels the routing uses: " + error);
}

void MainComponent::openRouting()
{
    // Routing lives in Edit (decision: design v3, milestone 11).
    if (isShowingStage())
        setStageVisible (false);
}

bool MainComponent::handleGoToKey (const juce::KeyPress& key)
{
    const auto now = juce::Time::getMillisecondCounter();

    if (goToNumber.isNotEmpty() && now > goToUntil)
        goToNumber.clear();   // forgotten: start over

    const auto c = key.getTextCharacter();

    if (c >= '0' && c <= '9' && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown())
    {
        goToNumber << juce::String::charToString (c);
        goToUntil = now + 5000;
        showNotice ("Go to " + session->getWords().item + " " + goToNumber + "_   (Enter to go, Esc to cancel)", 5);
        return true;
    }

    if (goToNumber.isEmpty())
        return false;

    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::backspaceKey)
    {
        goToNumber = goToNumber.dropLastCharacters (1);
        showNotice (goToNumber.isEmpty() ? juce::String() : "Go to " + session->getWords().item + " " + goToNumber + "_", goToNumber.isEmpty() ? 0 : 5);
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        goToNumber.clear();
        showNotice ({}, 0);
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        const auto index = session->findCueByNumber (goToNumber);

        if (index >= 0)
        {
            session->goToCue (index);
            showNotice ({}, 0);
            eventlog::write ("Go to cue " + goToNumber);
        }
        else
        {
            showNotice ("There's no " + session->getWords().item + " " + goToNumber, 4);
        }

        goToNumber.clear();
        return true;
    }

    return false;
}

void MainComponent::setCueListVisible (bool shouldShow)
{
    cueEditor->setVisible (shouldShow);

    if (shouldShow)
    {
        cueEditor->refresh();
        cueEditor->toFront (false);

        if (stageView != nullptr && stageView->isVisible())
            stageView->toFront (false);
    }

    resized();

    if (isShowing())   // (off-screen in tests)
        grabKeyboardFocus();
}

void MainComponent::showStageSettingsMenu (juce::PopupMenu& menu)
{
    const auto& stage = session->getStage();

    menu.addItem ("Tuner mutes the output", true, stage.tunerMutes, [this, mutes = stage.tunerMutes] { session->setTunerMutes (! mutes); });

    juce::PopupMenu pitch;

    for (const auto hz : { 415.0f, 430.0f, 432.0f, 438.0f, 439.0f, 440.0f, 441.0f, 442.0f, 443.0f, 444.0f, 445.0f, 446.0f })
        pitch.addItem ("A = " + juce::String (hz, 0) + " Hz", true, std::abs (stage.referencePitch - hz) < 0.01f,
                       [this, hz] { session->setReferencePitch (hz); });

    menu.addSubMenu ("Concert pitch", pitch);

    juce::PopupMenu panic;
    panic.addItem ("Silence, clear tails, then come back by itself", true, stage.panic == PanicBehaviour::recover,
                   [this] { session->setPanicBehaviour (PanicBehaviour::recover); });
    panic.addItem ("Silence until panic is pressed again", true, stage.panic == PanicBehaviour::latch,
                   [this] { session->setPanicBehaviour (PanicBehaviour::latch); });
    menu.addSubMenu ("Panic", panic);
}

void MainComponent::setStageVisible (bool shouldShow)
{
    if (stageView == nullptr || shouldShow == stageView->isVisible())
        return;

    if (! shouldShow)
    {
        stageView->setEditingLayout (false);
        stageView->setLocked (false);
    }

    stageView->setVisible (shouldShow);

    if (shouldShow)
        stageView->toFront (false);

    if (onViewChanged != nullptr)
        onViewChanged();   // the window resizes us: full window for the stage

    resized();

    if (isShowing())   // (off-screen in tests)
        grabKeyboardFocus();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
            window->setFullScreen (! window->isFullScreen());

        return true;
    }

    // Locked: the keyboard changes nothing (unlock by holding the lock).
    if (isShowingStage() && stageView->isLocked())
        return true;

    if (key.getKeyCode() == juce::KeyPress::F9Key)
    {
        openPreShowCheck();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F5Key)
    {
        setStageVisible (! isShowingStage());
        return true;
    }

    if (key == juce::KeyPress ('t') || key == juce::KeyPress ('T'))
    {
        engine.setTunerOpen (! engine.isTunerOpen());
        return true;
    }

    // I: the layout editor's widget inspector.
    if ((key == juce::KeyPress ('i') || key == juce::KeyPress ('I')) && isShowingStage() && stageView->isEditingLayout())
    {
        stageView->setLayoutInspectorShown (! stageView->isLayoutInspectorShown());
        return true;
    }

    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0) && isShowingStage())
    {
        stageView->setLocked (true);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F6Key && ! isShowingStage())
    {
        setCueListVisible (! cueEditor->isVisible());
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::escapeKey && engine.isTunerOpen())
    {
        engine.setTunerOpen (false);
        return true;
    }

    // "Go to cue 42": type the number, then Enter.
    if (handleGoToKey (key))
        return true;

    if (cueEditor->isVisible() && ! isShowingStage() && cueEditor->handleKey (key))
        return true;

    // Step through the set list from the computer keyboard (the foot
    // controller does the same from milestone 6).
    const auto code = key.getKeyCode();

    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)
        {
            session->next();
            return true;
        }

        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey)
        {
            session->previous();
            return true;
        }
    }

    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        session->save();
        reportProblems();
        return true;
    }

    return false;
}

//==============================================================================
void MainComponent::openAudioSettings()   { openSettings (SettingsWindow::Tab::audio); }
void MainComponent::openMidiSettings()    { openSettings (SettingsWindow::Tab::midi); }

void MainComponent::openSettings (SettingsWindow::Tab tab)
{
    // One dialog with a tab each (Audio, MIDI, Plugins), over the window (SCREENS.md §7).
    if (settingsWindow == nullptr)
    {
        settingsWindow = std::make_unique<SettingsWindow> (
            deviceManager, tuning, [this] { engine.resetCounters(); },
            engine.getMidiRouter(), settings, *session, *plugins,
            [this] { applyMidiBindings(); }, [this] { applyMidiOutput(); },
            [this]
            {
                // Closed from one of its own buttons: let the click return first.
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                {
                    if (safe != nullptr)
                        safe->closeSettings();
                });
            });

        auto& overlay = settingsWindow->getOverlay();
        addAndMakeVisible (overlay);
        overlay.setBounds (getLocalBounds());
        overlay.toFront (true);
        overlay.enterModalState (isShowing());   // focus only when on screen (not in off-screen tests)
    }

    settingsWindow->showTab (tab);
    settingsWindow->setAudioStatus (audioStatus);
}

void MainComponent::closeSettings()
{
    if (settingsWindow == nullptr)
        return;

    settingsWindow->getOverlay().exitModalState (0);
    settingsWindow = nullptr;

    if (isShowing())
        grabKeyboardFocus();
}

void MainComponent::applyMidiBindings()
{
    // The show's own mappings if it has them, else this computer's.
    const auto& show = session->getShow();
    auto bindings = show.ownMidiMappings ? show.midiBindings : settings.getMidiBindings();

    if (bindings != appliedBindings)
    {
        appliedBindings = bindings;
        engine.getMidiRouter().setBindings (bindings);
    }
}

void MainComponent::applyMidiOutput()
{
    // Open the chosen output if it's plugged in; close it if it went away.
    // Called when the choice changes and every 2 s (USB/Bluetooth reconnects).
    const auto wanted = settings.getMidiOutput();
    bool present = false;

    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        present = present || device.identifier == wanted;

    if (wanted == openMidiOutput && present == midiOutputPresent)
        return;

    openMidiOutput = wanted;
    midiOutputPresent = present;
    engine.getMidiRouter().setOutput (present && wanted.isNotEmpty() ? juce::MidiOutput::openDevice (wanted) : nullptr);
}

void MainComponent::refreshInputChoices()
{
    inputChoice.clear (juce::dontSendNotification);

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    // The switched-on input channels; an item's id is the channel (1-based).
    const auto names = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();

    int count = 0;

    for (int ch = 0; ch < names.size(); ++ch)
        if (active[ch])
        {
            inputChoice.addItem (juce::String (ch + 1) + ": " + names[ch], ch + 1);
            ++count;
        }

    if (count == 0)
    {
        setWarning ("No input channels enabled: open Audio settings and tick your guitar input.");
        return;
    }

    // The first input's channel. One that isn't switched on shows nothing
    // chosen (and plays silence) rather than quietly changing the routing.
    inputChoice.setSelectedId (engine.getRouting().inputs.front().channel, juce::dontSendNotification);
}

void MainComponent::refreshSystemStatus()
{
   #if JUCE_WINDOWS
    juce::StringArray parts;
    parts.add (tuning.highPriority ? "high priority" : "normal priority (couldn't raise)");
    parts.add (tuning.powerThrottlingDisabled ? "power throttling off" : "power throttling ON (couldn't disable)");
    parts.add (system::isAudioThreadProAudio() ? "Pro Audio audio thread" : "audio thread not yet registered");

    auto text = "Windows: " + parts.joinIntoString (", ");
    auto problem = ! tuning.highPriority || ! tuning.powerThrottlingDisabled;

    if (system::getPowerSource() == system::PowerSource::battery)
    {
        text << "   |   ON BATTERY: plug in for shows";
        problem = true;
    }

    if (text != systemText || problem != systemProblem)
    {
        systemText = text;
        systemProblem = problem;
        refreshWarningLine();
    }
   #endif
}

void MainComponent::refreshStatus()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    refreshWarningLine();   // lost, or back

    if (deviceLost)
    {
        deviceLine.setText ("Reconnecting to " + lastGoodSetup.outputDeviceName + "...", juce::dontSendNotification);
        deviceLine.setTooltip ({});
        latencyLine.setText ({}, juce::dontSendNotification);
        latencyLine.setTooltip ({});
        stageDeviceLine = deviceLine.getText();
        roundTripMs = 0.0;
        refreshLcd();
        return;
    }

    if (device == nullptr || ! device->isOpen())
    {
        deviceLine.setText ("No audio device open", juce::dontSendNotification);
        deviceLine.setTooltip ({});
        latencyLine.setText ({}, juce::dontSendNotification);
        latencyLine.setTooltip ({});
        stageDeviceLine = deviceLine.getText();
        roundTripMs = sampleRate = 0.0;
        refreshLcd();
        return;
    }

    const auto rate = device->getCurrentSampleRate();
    const auto buffer = device->getCurrentBufferSizeSamples();
    const auto typeName = device->getTypeName();

    // Today's full status lines live on in the tooltips.
    const auto deviceStatus = typeName + "  |  " + device->getName()
                              + "  |  " + juce::String (rate / 1000.0, 1) + " kHz"
                              + "  |  buffer " + juce::String (buffer) + " samples ("
                              + formatMs (buffer, rate) + ")";

    // ASIO drivers report their full input and output latency (buffers plus
    // converters), so the sum is the time from string to speaker.
    const auto inLatency = device->getInputLatencyInSamples();
    const auto outLatency = device->getOutputLatencyInSamples();
    auto* chain = engine.getChain();
    const auto chainLatency = chain != nullptr ? chain->getLatencySamples() : 0;
    auto latency = "Latency: in " + formatMs (inLatency, rate) + " + out " + formatMs (outLatency, rate);

    if (chainLatency > 0)
        latency << " + chain " << formatMs (chainLatency, rate);

    latency << " = " << formatMs (inLatency + outLatency + chainLatency, rate) << " round trip";

    deviceLine.setText (device->getName() + sep() + typeName, juce::dontSendNotification);
    deviceLine.setTooltip (deviceStatus);
    latencyLine.setText (juce::String (rate / 1000.0, 1) + " kHz" + sep() + juce::String (buffer) + " samples" + sep()
                             + formatMs (inLatency + outLatency + chainLatency, rate) + " round trip",
                         juce::dontSendNotification);
    latencyLine.setTooltip (deviceStatus + "\n" + latency);
    stageDeviceLine = typeName + sep() + latencyLine.getText();   // "ASIO · 48 kHz · 64 samples · 5.3 ms round trip"

    sampleRate = rate;
    roundTripMs = rate > 0.0 ? 1000.0 * (inLatency + outLatency + chainLatency) / rate : 0.0;
    refreshLcd();

    if (typeName != "ASIO")
        setWarning ("Not using ASIO: expect higher latency. Pick an ASIO driver in Audio settings for the stage.");
    else if (warning.startsWith ("Not using ASIO"))
        setWarning ({});
}

void MainComponent::refreshLcd()
{
    if (session == nullptr)
        return;

    // n / N and the item word; the playing item's name and its patch; round
    // trip; rate (SCREENS.md §4.1). A dash where there's nothing to show.
    const auto dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
    const auto& words = session->getWords();
    const auto cues = session->getCues();
    const auto cue = session->getCurrentCue();
    const auto* patch = session->getShow().findPatch (session->getActivePatchId());
    const auto playing = juce::isPositiveAndBelow (cue, (int) cues.size());

    juce::String name = playing ? cues[(size_t) cue]->name : juce::String();

    if (name.isEmpty() && patch != nullptr)
        name = patch->name;

    lcd.setItems ({
        { (playing ? juce::String (cue + 1) : dash) + " / " + juce::String ((int) cues.size()), words.item, 0 },
        { name.isNotEmpty() ? name : dash, "Patch " + (patch != nullptr ? patch->name : dash), size::lcdMinPatch, size::lcdNameMaxW },
        { roundTripMs > 0.0 ? juce::String (roundTripMs, 1) + " ms" : dash, "Round trip", 0 },
        { sampleRate > 0.0 ? juce::String (sampleRate / 1000.0, 1) + "k" : dash, "Rate", 0 },
    });

    if (lcd.getIdealWidth() != lcd.getWidth())
        layoutToolbar (toolbar.getLocalBounds());
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == session.get())
    {
        applyMidiBindings();   // another show may carry its own
        setList->refresh();

        if (cueEditor != nullptr && cueEditor->isVisible() && ! cueEditor->isTyping())
            cueEditor->refresh();
        patchList->refresh();
        refreshShowStatus();
        reportProblems();

        // The crash guard switched a plugin off: say so at once.
        for (const auto& text : session->takePluginNotices())
        {
            showNotice (text, 20, true);
            eventlog::write ("Plugin switched off - " + text);
        }

        syncPluginWindows();
        return;
    }

    // The device, rate, buffer or channels changed: remember them for next launch.
    if (auto state = deviceManager.createStateXml())
        settings.setAudioDeviceState (state.get());

    if (warning.startsWith ("No input") || warning.startsWith ("Audio device problem"))
        setWarning ({});

    // Another interface: switch on the channels the routing uses (once; on
    // the same interface, what Settings › Audio switches off stays off).
    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->getName() != lastRoutedDevice)
    {
        lastRoutedDevice = device->getName();
        ensureRoutingChannelsActive();
    }

    refreshInputChoices();
    refreshStatus();
}

void MainComponent::timerCallback()
{
    watchAudioDevice();
    engine.collectGarbage();
    session->syncWithAudio();   // cue or patch changed on the audio thread

    // Take the keyboard back (e.g. after a dialog closed), unless a dialog is open.
    // ...but not from a plugin's own window the player is typing in.
    if (frameCounter % 15 == 0 && isShowing() && ! hasKeyboardFocus (true)
        && juce::Component::getCurrentlyModalComponent() == nullptr && juce::Process::isForegroundProcess()
        && ! isPluginWindowActive())
        grabKeyboardFocus();

    // A newly built chain arrived (patch selected, block added...).
    if (auto* chain = engine.getChain(); chain != chainStrip->getChain())
    {
        chainStrip->setChain (chain);
        showSelectedBlock();
        refreshStatus();
        refreshShowStatus();
    }

    chainStrip->refresh();

    if (blockPanel != nullptr)
        blockPanel->refresh();

    inspector->refresh();

    // Plugin windows without an editor of their own: their parameter lists.
    for (const auto& [id, window] : pluginWindows)
        window->refresh();

    // Outputs muted from Stage or a footswitch: kept in this computer's routing.
    if (engine.syncOutputMutes())
        settings.setRouting (engine.getRouting());

    // A footswitch can mute too: the button follows the engine.
    if (const auto isMuted = engine.isMuted(); muteButton.getToggleState() != isMuted)
    {
        muteButton.setToggleState (isMuted, juce::dontSendNotification);
        muteButton.setButtonText (isMuted ? "MUTED" : "LIVE");
    }

    // Meters are taken once and shared by both views.
    StageSnapshot snapshot;
    snapshot.inputPeak = engine.getInputMeter().take();
    snapshot.outputPeak = engine.getOutputMeter().take();
    snapshot.cpu = engine.getCpuLoad();
    snapshot.overruns = engine.getOverrunCount();
   #if JUCE_WINDOWS
    snapshot.onBattery = system::getPowerSource() == system::PowerSource::battery;
   #endif

    inputMeter.setPeak (snapshot.inputPeak);
    outputMeter.setPeak (snapshot.outputPeak);

    if (deviceLost)
        snapshot.audioProblem = "AUDIO INTERFACE LOST - reconnecting to " + lastGoodSetup.outputDeviceName + "...";

    // The stage footer: the device line (the lost text while lost) and the footswitch.
    snapshot.deviceLine = deviceLost ? snapshot.audioProblem + " (staying on the cue; check the cable)" : stageDeviceLine;
    snapshot.footswitch = footswitchLine;
    snapshot.roundTripMs = deviceLost ? 0.0 : roundTripMs;

    if (juce::Time::getMillisecondCounter() < noticeUntil)
        snapshot.notice = notice;
    else if (noticeBar.isVisible())
        showNotice ({}, 0);   // the notice ran out

    outputMeter.setMuted (engine.isMuted());

    const auto tunerOpen = engine.isTunerOpen();

    if (tunerOpen)
        snapshot.tuner = engine.getTuner().analyse();

    tunerButton.setToggleState (tunerOpen, juce::dontSendNotification);
    panicButton.setButtonText (engine.getPanicState() == AudioEngine::PanicState::none ? "PANIC" : "SILENT");

    if (isShowingStage())
    {
        stageView->update (snapshot);
        editTuner.setVisible (false);
    }
    else
    {
        if (tunerOpen != editTuner.isVisible())
        {
            editTuner.setVisible (tunerOpen);
            editTuner.toFront (false);
        }

        if (tunerOpen)
            editTuner.setReading (snapshot.tuner, engine.getTunerMutes(), engine.getTuner().getReferencePitch());
    }

    // Text changes ~3x a second so numbers stay readable.
    if (++frameCounter % 10 != 0)
        return;

    refreshShowStatus();

    updateRecoveryFile();

    if (frameCounter % 60 == 0 || footswitchLine.isEmpty())   // power source, MIDI output reconnects etc. every 2 s
    {
        refreshSystemStatus();
        applyMidiOutput();

        // A foot controller is any MIDI input that's plugged in and switched on.
        bool footswitch = false;

        for (const auto& input : juce::MidiInput::getAvailableDevices())
            footswitch = footswitch || deviceManager.isMidiInputDeviceEnabled (input.identifier);

        footswitchLine = footswitch ? "Footswitch connected" : "No footswitch connected";
    }

    juce::String health;
    health << "CPU " << juce::roundToInt (engine.getCpuLoad() * 100.0) << " %"
           << "   |   overruns " << engine.getOverrunCount();

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto driverXruns = device->getXRunCount();

        if (driverXruns >= 0)
            health << "   |   driver dropouts " << driverXruns;
    }

    const auto limiting = engine.takeLimiterReductionDb();
    health << "   |   limiter " << (limiting < -0.1f ? juce::String (limiting, 1) + " dB" : juce::String ("idle"));

    if (engine.getClipCount() > 0)
        health << "   |   clips " << engine.getClipCount();

    if (const auto ringing = engine.getPlayer().getNumRinging(); ringing > 0)
        health << "   |   ringing out " << ringing;

    if (const auto bytes = system::getProcessMemoryBytes(); bytes > 0)
        health << "   |   memory " << (int) (bytes / (1024 * 1024)) << " MB of "
               << juce::SystemStats::getMemorySizeInMegabytes() / 1024 << " GB";

    // Debug builds only: allocations on the audio thread (must stay 0).
    if (rt::isAllocationTripwireInstalled())
        health << "   |   RT violations " << rt::getViolationCount();

    // The settings window's status values (the same data).
    {
        AudioSettingsView::Status st;
        st.cpu = juce::String (juce::roundToInt (engine.getCpuLoad() * 100.0)) + "%";
        st.overruns = juce::String (engine.getOverrunCount());
        auto* device = deviceManager.getCurrentAudioDevice();
        st.driverDropouts = device != nullptr && device->getXRunCount() >= 0 ? juce::String (device->getXRunCount()) : juce::String ("-");
        st.limiter = limiting < -0.1f ? juce::String (limiting, 1) + " dB" : juce::String ("idle");

        if (const auto bytes = system::getProcessMemoryBytes(); bytes > 0)
            st.memory = juce::String ((int) (bytes / (1024 * 1024))) + " MB of " + juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024) + " GB";
        else
            st.memory = "-";

        if (device != nullptr && device->isOpen() && sampleRate > 0.0)
            st.latency = "in " + juce::String (1000.0 * device->getInputLatencyInSamples() / sampleRate, 1) + " + out "
                         + juce::String (1000.0 * device->getOutputLatencyInSamples() / sampleRate, 1) + " = " + juce::String (roundTripMs, 1) + " ms";
        else
            st.latency = "-";

        audioStatus = st;

        if (settingsWindow != nullptr)
            settingsWindow->setAudioStatus (audioStatus);
    }

    // Short line in the pane, today's full line in the tooltip.
    juce::String shortHealth;
    shortHealth << "CPU " << juce::roundToInt (engine.getCpuLoad() * 100.0) << "%" << sep() << "overruns " << engine.getOverrunCount();

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->getXRunCount() >= 0)
        shortHealth << sep() << "dropouts " << device->getXRunCount();

    shortHealth << sep() << "limiter " << (limiting < -0.1f ? juce::String (limiting, 1) + " dB" : juce::String ("idle"));

    if (engine.getClipCount() > 0)
        shortHealth << sep() << "clips " << engine.getClipCount();

    healthLine.setText (shortHealth, juce::dontSendNotification);
    healthLine.setTooltip (health);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::window));

    if (! headerArrow.isEmpty() && ! isShowingStage())
        drawIcon (g, Icon::arrowRight, headerArrow.toFloat(), paint::c (colour::textMuted));
}

juce::Point<int> MainComponent::getMinimumEditSize() const
{
    // The reference minimum (SCREENS.md §1); the block stage scales the gear
    // to whatever room it gets.
    return { size::minEditWidth, size::minEditHeight };
}

void MainComponent::layoutToolbar (juce::Rectangle<int> bar)
{
    // Padding 10, gaps 10; the LCD centred in what's left (SCREENS.md §4.1).
    const auto buttonH = size::buttonHeight + size::buttonDrop;
    const auto buttonY = (bar.getHeight() - 1 - size::buttonHeight) / 2;
    const auto place = [&] (juce::Component& c, int x, int w, int h, int y) { c.setBounds (x, y, w, h); };
    const auto widthOf = [buttonH] (juce::TextButton& b) { return b.getBestWidthForHeight (buttonH); };

    auto left = bar.getX() + size::toolbarPadX;
    toolbar.setLogoBounds ({ left + size::logoMargin, (bar.getHeight() - 1 - size::logoH) / 2, size::logoW, size::logoH });
    left += size::logoMargin + size::logoW + size::logoMargin + size::toolbarGap;

    place (showButton, left, widthOf (showButton), buttonH, buttonY);
    left = showButton.getRight() + size::toolbarGap;

    place (*showTitle, left, juce::jmin (showTitle->getIdealWidth(), size::showTitleMaxW), bar.getHeight() - 1, 0);   // long names are cut
    left = showTitle->getRight() + size::toolbarGap + size::modeSwitchGap;

    const auto modeH = size::toolbarButtonH + 2;
    place (modeSwitch, left, size::modeSegmentW * 3 + 2, modeH, (bar.getHeight() - 1 - modeH) / 2);
    left = modeSwitch.getRight() + size::toolbarGap;

    auto right = bar.getRight() - size::toolbarPadX;
    const auto fromRight = [&] (juce::Component& c, int w, int h, int y, int gapAfter)
    {
        right -= w;
        place (c, right, w, h, y);
        right -= gapAfter;
    };

    fromRight (muteButton, size::muteButtonW, buttonH, buttonY, size::toolbarGap);
    fromRight (panicButton, widthOf (panicButton), buttonH, buttonY, size::toolbarGap);

    // Out: label, 6, [thumb room 6, 70 track, 6, 66 field]
    const auto volumeW = getLookAndFeel().getSliderThumbRadius (volume) * 2 + size::volumeTrackW + size::valueFieldW;
    fromRight (volume, volumeW, bar.getHeight() - 1, 0, 0);
    right += getLookAndFeel().getSliderThumbRadius (volume) - size::outGap;   // the track starts 6 after "Out"
    const auto outW = (int) std::ceil (textWidth (font (type::label), outLabel.getText()));
    fromRight (outLabel, outW, bar.getHeight() - 1, 0, size::outMargin + size::toolbarGap);

    fromRight (audioSettingsButton, widthOf (audioSettingsButton), buttonH, buttonY, size::toolbarGap);
    fromRight (midiSettingsButton, widthOf (midiSettingsButton), buttonH, buttonY, size::toolbarGap);
    fromRight (tunerButton, widthOf (tunerButton), buttonH, buttonY, size::toolbarGap);

    // The LCD centred between the two groups; hidden if it doesn't fit.
    const auto space = right + size::toolbarGap - left;
    lcd.setAvailableWidth (space);
    const auto lcdW = lcd.getIdealWidth(), lcdH = kit::Lcd::heightFor (false);
    lcd.setVisible (lcdW <= space);
    place (lcd, left + (space - lcdW) / 2, lcdW, lcdH, (bar.getHeight() - 1 - (lcdH - 1)) / 2);
}

void MainComponent::layoutBottomRow (juce::Rectangle<int> row)
{
    inputPane.setBounds (row.removeFromLeft (size::sidebarWidth));
    row.removeFromLeft (size::gap);
    healthPane.setBounds (row.removeFromRight (size::inspectorWidth));
    row.removeFromRight (size::gap);
    metersPane.setBounds (row);

    // Guitar input: label, field, device line, centred (gap 6).
    {
        auto r = inputPane.getLocalBounds().reduced (1 + size::inputPadX, 0);
        const auto h = size::inputLabelH + size::inputGap + size::fieldHeight + size::inputGap + size::deviceLineH;
        r = r.withSizeKeepingCentre (r.getWidth(), h);
        inputLabel.setBounds (r.removeFromTop (size::inputLabelH));
        r.removeFromTop (size::inputGap);
        inputChoice.setBounds (r.removeFromTop (size::fieldHeight));
        r.removeFromTop (size::inputGap);
        deviceLine.setBounds (r);
    }

    // Meters: two rows, the scale under them (painted by paint()).
    {
        auto r = metersPane.getLocalBounds().reduced (1 + size::metersPadX, 0);
        const auto h = size::meterRowH * 2 + size::meterRowGap + size::meterScaleGap + size::meterScaleH;
        r = r.withSizeKeepingCentre (r.getWidth(), h);
        inputMeter.setBounds (r.removeFromTop (size::meterRowH));
        r.removeFromTop (size::meterRowGap);
        outputMeter.setBounds (r.removeFromTop (size::meterRowH));
        r.removeFromTop (size::meterScaleGap);
        meterScale.setBounds (r.removeFromTop (size::meterScaleH));

        // The scale lines up with the meters' tracks.
        const auto track = inputMeter.getTrackBounds();
        meterScale.setTrack (track.getX() + (float) (inputMeter.getX() - meterScale.getX()), track.getWidth());
    }

    // System health: title and Reset counters, then three lines (gap 3).
    {
        auto r = healthPane.getLocalBounds().reduced (1 + size::healthPadX, 1 + size::healthPadY);
        auto header = r.removeFromTop (size::healthHeaderH);
        const auto resetW = resetCountersButton.getBestWidthForHeight (size::buttonSmallHeight);
        resetCountersButton.setBounds (header.removeFromRight (resetW).withHeight (size::buttonSmallHeight + size::buttonDrop));
        healthTitle.setBounds (header);

        for (auto* line : { &healthLine, &latencyLine })
        {
            r.removeFromTop (size::healthGap);
            line->setBounds (r.removeFromTop (size::healthLineH));
        }

        r.removeFromTop (size::healthGap);
        warningLine.setBounds (r);   // may wrap to two lines
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    if (stageView != nullptr)
        stageView->setBounds (area);

    if (checkDialog != nullptr)
        checkDialog->getOverlay().setBounds (area);

    if (settingsWindow != nullptr)
        settingsWindow->getOverlay().setBounds (area);

    toolbar.setBounds (area.removeFromTop (size::toolbarEdit + 1));
    layoutToolbar (toolbar.getLocalBounds());

    if (noticeBar.isVisible())
        noticeBar.setBounds (area.removeFromTop (size::noticeBar + 1));

    area.reduce (size::outerPadding, size::outerPadding);
    layoutBottomRow (area.removeFromBottom (size::bottomRow));
    area.removeFromBottom (size::outerPadding);

    // Main grid: sidebar 250 | centre | inspector 272 (SCREENS.md §4.3).
    // The full list covers all three columns.
    if (cueEditor != nullptr)
        cueEditor->setBounds (area);

    auto sidebar = area.removeFromLeft (size::sidebarWidth);
    area.removeFromLeft (size::gap);
    if (inspector != nullptr)
        inspector->setBounds (area.removeFromRight (size::inspectorWidth));
    else
        area.removeFromRight (size::inspectorWidth);
    area.removeFromRight (size::gap);

    if (setList != nullptr && patchList != nullptr)
    {
        setList->setBounds (sidebar.removeFromTop ((sidebar.getHeight() - size::gap) / 2));
        sidebar.removeFromTop (size::gap);
        patchList->setBounds (sidebar);
    }

    // Centre: header line, chain, block. The tuner covers the centre column.
    editTuner.setBounds (area);
    auto header = area.removeFromTop (size::centreHeaderH);
    const auto problemsW = problemsLabel.getText().isEmpty() ? 0
                               : juce::jmin (header.getWidth() / 2, (int) std::ceil (textWidth (font (type::label), problemsLabel.getText())) + 1);
    problemsLabel.setBounds (header.removeFromRight (problemsW));

    // "Cue 1 of 8: Clean" → [Patch: Clean ▾] (loading patches...)
    const auto labelW = juce::jmin (header.getWidth(), (int) std::ceil (textWidth (font (type::headerLine), chainLabel.getText())) + 1);
    chainLabel.setBounds (header.removeFromLeft (labelW));
    headerArrow = {};

    if (patchPicker.isVisible())
    {
        header.removeFromLeft (size::toolbarGap);
        headerArrow = header.removeFromLeft (size::headerArrow).withSizeKeepingCentre (size::headerArrow, size::headerArrow);
        header.removeFromLeft (size::toolbarGap);
        patchPicker.setBounds (header.removeFromLeft (size::patchPickerW).withSizeKeepingCentre (size::patchPickerW, size::fieldHeight));
    }

    header.removeFromLeft (size::toolbarGap);
    loadingLabel.setBounds (header);
    area.removeFromTop (size::gap);

    if (chainStrip != nullptr)
        chainStrip->setBounds (area.removeFromTop (size::chainPaneH));

    area.removeFromTop (size::gap);

    if (blockPanel != nullptr)
        blockPanel->setBounds (area);   // the block stage fills the rest
}

//==============================================================================
void MainComponent::startFirstPluginScanIfNeeded()
{
    // The first start: find the installed plugins in the background.
    if (! settings.getFolder().getChildFile ("plugins.xml").existsAsFile())
        plugins->startScan();
}

void MainComponent::openPluginWindow (PluginBlock& block)
{
    const auto id = block.getModelId();

    if (auto it = pluginWindows.find (id); it != pluginWindows.end())
    {
        if (&it->second->getBlock() == &block)
        {
            it->second->toFront (true);
            return;
        }

        closePluginWindow (id, false);
    }

    if (block.getInstance() == nullptr)
        return;

    const auto* chain = engine.getChain();
    auto window = std::make_unique<PluginWindow> (block, chain != nullptr ? chain->label : juce::String(), [this, id]
    {
        // Closed with its own button: after the click returns.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), id]
        {
            if (safe != nullptr)
                safe->closePluginWindow (id, false);
        });
    });

    if (const auto it = pluginWindowBounds.find (id); it != pluginWindowBounds.end())
        window->setTopLeftPosition (it->second.getPosition());
    else
        window->centreAroundComponent (this, window->getWidth(), window->getHeight());

    window->setVisible (true);
    window->toFront (true);
    pluginWindows[id] = std::move (window);
}

void MainComponent::closePluginWindow (const juce::String& blockId, bool reopenWhenRebuilt)
{
    const auto it = pluginWindows.find (blockId);

    if (it == pluginWindows.end())
        return;

    pluginWindowBounds[blockId] = it->second->getBounds();

    if (reopenWhenRebuilt && it->second->getBlock().getFault() != PluginBlock::Fault::crashed)
        pluginWindowsToReopen.addIfNotAlreadyThere (blockId);

    pluginWindows.erase (it);
}

void MainComponent::syncPluginWindows()
{
    // A crashed plugin's window goes (without touching the plugin).
    std::vector<juce::String> crashed;

    for (const auto& [id, window] : pluginWindows)
        if (window->getBlock().getFault() == PluginBlock::Fault::crashed)
            crashed.push_back (id);

    for (const auto& id : crashed)
        closePluginWindow (id, false);

    // Windows closed because their patch was rebuilt come back for the new plugin.
    if (pluginWindowsToReopen.isEmpty())
        return;

    auto* chain = engine.getChain();

    if (chain == nullptr || session->isBuilding())
        return;

    for (const auto& block : chain->getBlocks())
        if (auto* plugin = dynamic_cast<PluginBlock*> (block.get()); plugin != nullptr && pluginWindowsToReopen.contains (plugin->getModelId()))
            openPluginWindow (*plugin);

    pluginWindowsToReopen.clear();
}

bool MainComponent::isPluginWindowActive() const
{
    for (const auto& [id, window] : pluginWindows)
        if (window->isActiveWindow())
            return true;

    return false;
}

} // namespace encore
