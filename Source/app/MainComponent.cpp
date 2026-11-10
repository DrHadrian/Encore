// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/MainComponent.h"

#include "engine/blocks/NoiseGateBlock.h"
#include "engine/core/Realtime.h"

namespace encore
{

namespace
{
    const auto background = juce::Colour (0xff121417);
    const auto barColour  = juce::Colour (0xff181b20);
    const auto mutedRed   = juce::Colour (0xffc62f35);
    const auto liveGreen  = juce::Colour (0xff2e9e5b);
    const auto amber      = juce::Colour (0xfff0b429);

    void styleLabel (juce::Label& label, float size, float alpha = 0.85f)
    {
        label.setFont (juce::FontOptions (size));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (alpha));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::String formatMs (int samples, double sampleRate)
    {
        return juce::String (1000.0 * samples / sampleRate, 1) + " ms";
    }

    juce::File defaultShowsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Encore").getChildFile ("Shows");
    }
}

//==============================================================================
MainComponent::MainComponent (AppSettings& s) : settings (s)
{
    // What DAWs do so Windows never slows the audio down (see SystemTuning.h).
    tuning = system::applyLiveProcessSettings();

    setWantsKeyboardFocus (true);

    // Top bar
    title.setText ("Encore", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    showName.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    showName.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (showName);

    styleLabel (saveState, 14.0f, 0.6f);
    addAndMakeVisible (saveState);

    showButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (showButton);

    audioSettingsButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (audioSettingsButton);

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    volume.setTooltip ("Master volume");
    volume.setValue (settings.getMasterGainDecibels(), juce::dontSendNotification);
    engine.setMasterGainDecibels ((float) volume.getValue());
    volume.onValueChange = [this]
    {
        engine.setMasterGainDecibels ((float) volume.getValue());
        settings.setMasterGainDecibels ((float) volume.getValue());
    };
    addAndMakeVisible (volume);

    // Encore always starts muted: a laptop booting next to a hot PA must not
    // blast. Unmuting is one deliberate click (a footswitch, from milestone 6).
    muteButton.setClickingTogglesState (true);
    muteButton.setToggleState (true, juce::dontSendNotification);
    muteButton.onClick = [this]
    {
        const auto isMuted = muteButton.getToggleState();
        engine.setMuted (isMuted);
        muteButton.setButtonText (isMuted ? "MUTED" : "LIVE");
    };
    muteButton.setColour (juce::TextButton::buttonOnColourId, mutedRed);
    muteButton.setColour (juce::TextButton::buttonColourId, liveGreen);
    addAndMakeVisible (muteButton);

    // Audio status
    for (auto* label : { &deviceStatus, &latencyStatus, &healthStatus })
    {
        styleLabel (*label, 14.0f, 0.75f);
        addAndMakeVisible (label);
    }

    styleLabel (systemStatus, 13.0f, 0.55f);
    addAndMakeVisible (systemStatus);

    styleLabel (warning, 14.0f);
    warning.setColour (juce::Label::textColourId, amber);
    addAndMakeVisible (warning);

    styleLabel (inputLabel, 14.0f);
    addAndMakeVisible (inputLabel);

    inputChoice.onChange = [this]
    {
        const auto index = inputChoice.getSelectedId() - 1;   // ids are 1-based

        if (index >= 0)
        {
            engine.setGuitarInputChannel (index);
            settings.setGuitarInputChannel (index);
        }
    };
    addAndMakeVisible (inputChoice);

    resetCountersButton.onClick = [this] { engine.resetCounters(); };
    addAndMakeVisible (resetCountersButton);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    styleLabel (chainLabel, 17.0f, 1.0f);
    chainLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    addAndMakeVisible (chainLabel);

    styleLabel (problemsLabel, 14.0f);
    problemsLabel.setColour (juce::Label::textColourId, amber);
    addAndMakeVisible (problemsLabel);

    // Open the audio device: the saved one if possible, else a sensible default.
    // Prefer ASIO on first run because it gives the lowest, most stable latency.
    auto savedState = settings.getAudioDeviceState();
    auto error = deviceManager.initialise (2, 2, savedState.get(), true);

    if (savedState == nullptr)
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() == "ASIO")
            {
                deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }

    if (error.isNotEmpty())
        warning.setText ("Audio device problem: " + error, juce::dontSendNotification);

    engine.setGuitarInputChannel (settings.getGuitarInputChannel());
    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    // The show: with the device running, patches build for the right rate.
    session = std::make_unique<ShowSession> (engine);
    session->addChangeListener (this);

    patchList = std::make_unique<PatchList> (*session);
    addAndMakeVisible (*patchList);

    chainStrip = std::make_unique<ChainStrip> (*session);
    chainStrip->onSelectionChanged = [this] (int) { showSelectedBlock(); };
    addAndMakeVisible (*chainStrip);

    openInitialShow();

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

    blockPanel = nullptr;
    chainStrip = nullptr;
    patchList = nullptr;

    if (session != nullptr)
    {
        session->removeChangeListener (this);
        session = nullptr;   // saves unsaved edits
    }

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engine);
    settings.save();
}

//==============================================================================
void MainComponent::openInitialShow()
{
    const auto last = settings.getLastShowFolder();

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
        warning.setText ("Couldn't create a show: " + r.getErrorMessage(), juce::dontSendNotification);
    else
        settings.setLastShowFolder (folder);
}

void MainComponent::showMenu()
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
    menu.addItem ("Open backups folder", session->getFolder().isValid(), false,
                  [this] { session->getFolder().getBackupsFolder().startAsProcess(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&showButton));
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
    showName.setText (show.name, juce::dontSendNotification);
    showName.setTooltip (session->getFolder().getFolder().getFullPathName());

    if (session->hasUnsavedChanges())
        saveState.setText ("Unsaved changes (autosaves)", juce::dontSendNotification);
    else
        saveState.setText ("Saved " + session->getLastSaveTime().formatted ("%H:%M:%S"), juce::dontSendNotification);

    const auto* patch = show.findPatch (session->getActivePatchId());
    chainLabel.setText (patch != nullptr ? patch->name + (session->isBuilding() ? "   (loading...)" : "") : juce::String(),
                        juce::dontSendNotification);
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

        addAndMakeVisible (*blockPanel);
    }

    relayout();
}

void MainComponent::relayout()
{
    // Inside a scrolling viewport: tell it our height may have changed.
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        viewport->resized();
    else
        resized();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        session->save();
        reportProblems();
        return true;
    }

    return false;
}

//==============================================================================
void MainComponent::openAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        deviceManager,
        1, 16,      // inputs: at least the guitar, up to 16
        1, 16,      // outputs
        false,      // MIDI inputs: own page from milestone 6
        false,      // MIDI output
        false,      // list channels individually, not as stereo pairs: guitar is mono
        false);     // show buffer size / sample rate without an extra click
    selector->setSize (560, 480);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::refreshInputChoices()
{
    inputChoice.clear (juce::dontSendNotification);

    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    // The engine sees only *active* input channels, packed in order, so the
    // combo lists those and maps item N to the Nth active channel.
    const auto names = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();

    int activeIndex = 0;

    for (int ch = 0; ch < names.size(); ++ch)
        if (active[ch])
            inputChoice.addItem (juce::String (ch + 1) + ": " + names[ch], ++activeIndex);

    if (activeIndex == 0)
    {
        warning.setText ("No input channels enabled: open Audio settings and tick your guitar input.",
                         juce::dontSendNotification);
        return;
    }

    const auto wanted = juce::jlimit (0, activeIndex - 1, engine.getGuitarInputChannel());
    inputChoice.setSelectedId (wanted + 1, juce::sendNotificationSync);
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

    systemStatus.setText (text, juce::dontSendNotification);
    systemStatus.setColour (juce::Label::textColourId, problem ? amber : juce::Colours::white.withAlpha (0.55f));
   #endif
}

void MainComponent::refreshStatus()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || ! device->isOpen())
    {
        deviceStatus.setText ("No audio device open", juce::dontSendNotification);
        latencyStatus.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto rate = device->getCurrentSampleRate();
    const auto buffer = device->getCurrentBufferSizeSamples();
    const auto typeName = device->getTypeName();

    deviceStatus.setText (typeName + "  |  " + device->getName()
                              + "  |  " + juce::String (rate / 1000.0, 1) + " kHz"
                              + "  |  buffer " + juce::String (buffer) + " samples ("
                              + formatMs (buffer, rate) + ")",
                          juce::dontSendNotification);

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
    latencyStatus.setText (latency, juce::dontSendNotification);

    if (typeName != "ASIO")
        warning.setText ("Not using ASIO: expect higher latency. Pick an ASIO driver in Audio settings for the stage.",
                         juce::dontSendNotification);
    else if (warning.getText().startsWith ("Not using ASIO"))
        warning.setText ({}, juce::dontSendNotification);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == session.get())
    {
        patchList->refresh();
        refreshShowStatus();
        reportProblems();
        return;
    }

    // The device, rate, buffer or channels changed: remember them for next launch.
    if (auto state = deviceManager.createStateXml())
        settings.setAudioDeviceState (state.get());

    if (warning.getText().startsWith ("No input") || warning.getText().startsWith ("Audio device problem"))
        warning.setText ({}, juce::dontSendNotification);

    refreshInputChoices();
    refreshStatus();
}

void MainComponent::timerCallback()
{
    engine.collectGarbage();

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

    inputMeter.setPeak (engine.getInputMeter().take());
    outputMeter.setPeak (engine.getOutputMeter().take());

    // Text changes ~3x a second so numbers stay readable.
    if (++frameCounter % 10 != 0)
        return;

    refreshShowStatus();

    if (frameCounter % 60 == 0)   // power source etc. every 2 s
        refreshSystemStatus();

    juce::String health;
    health << "CPU " << juce::String (engine.getCpuLoad() * 100.0, 0) << " %"
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

    // Debug builds only: allocations on the audio thread (must stay 0).
    if (rt::isAllocationTripwireInstalled())
        health << "   |   RT violations " << rt::getViolationCount();

    healthStatus.setText (health, juce::dontSendNotification);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (barColour);
    g.fillRect (getLocalBounds().removeFromTop (64));
}

void MainComponent::resized()
{
    layoutContent (getWidth(), true);
}

int MainComponent::getRequiredHeight (int width)
{
    return layoutContent (width, false);
}

int MainComponent::layoutContent (int width, bool apply)
{
    constexpr int margin = 16, gap = 16, sidebarWidth = 250;
    const auto place = [apply] (juce::Component* c, juce::Rectangle<int> r) { if (apply && c != nullptr) c->setBounds (r); };

    // Top bar
    auto bar = juce::Rectangle<int> (0, 0, width, 64).reduced (margin, 10);
    place (&title, bar.removeFromLeft (110));
    place (&showButton, bar.removeFromLeft (70).reduced (0, 4));
    bar.removeFromLeft (12);
    place (&muteButton, bar.removeFromRight (110).reduced (0, 2));
    bar.removeFromRight (8);
    place (&volume, bar.removeFromRight (200));
    bar.removeFromRight (8);
    place (&audioSettingsButton, bar.removeFromRight (150).reduced (0, 4));
    bar.removeFromRight (12);
    auto names = bar;
    place (&showName, names.removeFromTop (names.getHeight() / 2 + 2));
    place (&saveState, names);

    auto area = juce::Rectangle<int> (0, 64, width, 100000).reduced (margin, 0);
    area.removeFromTop (margin);

    // Body: patches left, chain + block right.
    const auto panelHeight = blockPanel != nullptr ? blockPanel->getPreferredHeight() : 200;
    const auto bodyHeight = juce::jmax (420, 30 + 120 + gap + panelHeight + 30);
    auto body = area.removeFromTop (bodyHeight);

    place (patchList.get(), body.removeFromLeft (sidebarWidth));
    body.removeFromLeft (gap);

    place (&chainLabel, body.removeFromTop (30));
    place (chainStrip.get(), body.removeFromTop (120));
    body.removeFromTop (gap);

    if (blockPanel != nullptr)
        place (blockPanel.get(), body.removeFromTop (panelHeight)
                                     .withWidth (juce::jmin (body.getWidth(), blockPanel->getPreferredWidth())));

    place (&problemsLabel, body.removeFromBottom (26));

    // Audio: input, meters, status lines.
    area.removeFromTop (gap);
    auto inputRow = area.removeFromTop (32);
    place (&inputLabel, inputRow.removeFromLeft (110));
    place (&inputChoice, inputRow.removeFromLeft (300).reduced (0, 2));
    inputRow.removeFromLeft (gap);
    place (&resetCountersButton, inputRow.removeFromRight (140).reduced (0, 2));

    area.removeFromTop (8);
    place (&inputMeter, area.removeFromTop (28));
    area.removeFromTop (6);
    place (&outputMeter, area.removeFromTop (28));
    area.removeFromTop (8);

    for (auto* label : { &healthStatus, &deviceStatus, &latencyStatus, &systemStatus, &warning })
        place (label, area.removeFromTop (22));

    return area.getY() + margin;
}

} // namespace encore
