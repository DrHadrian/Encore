// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/MainComponent.h"

#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/core/Realtime.h"

namespace encore
{

namespace
{
    const auto background = juce::Colour (0xff121417);
    const auto mutedRed   = juce::Colour (0xffc62f35);
    const auto liveGreen  = juce::Colour (0xff2e9e5b);

    void styleStatus (juce::Label& label, float size)
    {
        label.setFont (juce::FontOptions (size));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::String formatMs (int samples, double sampleRate)
    {
        return juce::String (1000.0 * samples / sampleRate, 1) + " ms";
    }
}

//==============================================================================
MainComponent::MainComponent (AppSettings& s) : settings (s)
{
    // What DAWs do so Windows never slows the audio down (see SystemTuning.h).
    tuning = system::applyLiveProcessSettings();

    title.setText ("Encore", juce::dontSendNotification);
    title.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    for (auto* label : { &deviceStatus, &latencyStatus, &healthStatus })
    {
        styleStatus (*label, 16.0f);
        addAndMakeVisible (label);
    }

    styleStatus (systemStatus, 14.0f);
    addAndMakeVisible (systemStatus);

    styleStatus (warning, 15.0f);
    warning.setColour (juce::Label::textColourId, juce::Colour (0xfff0b429));
    addAndMakeVisible (warning);

    for (auto* label : { &inputLabel, &volumeLabel })
    {
        styleStatus (*label, 16.0f);
        addAndMakeVisible (label);
    }

    audioSettingsButton.onClick = [this] { openAudioSettings(); };
    addAndMakeVisible (audioSettingsButton);

    resetCountersButton.onClick = [this] { engine.resetCounters(); };
    addAndMakeVisible (resetCountersButton);

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

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
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

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    styleStatus (chainLabel, 16.0f);
    addAndMakeVisible (chainLabel);

    // The chain is handed to the engine before audio starts, so the device
    // prepares it along with everything else.
    buildDefaultChain();

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

    refreshInputChoices();
    refreshStatus();
    refreshSystemStatus();

    // Now that the device is running, reload the amp from last time (in the
    // background; it fades in when ready).
    restoreLastFiles();

    setSize (1100, 860);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engine);
    settings.save();
}

//==============================================================================
void MainComponent::buildDefaultChain()
{
    // Test rig until patches arrive (milestone 4): amp, then a gate that
    // listens to the clean guitar (so it cuts amp hiss), then EQ.
    // The cab IR block slots in after the amp in milestone 3.
    auto chain = std::make_unique<Chain>();
    chain->addBlock (std::make_unique<NamAmpBlock>());
    chain->addBlock (std::make_unique<NoiseGateBlock>());
    chain->addBlock (std::make_unique<EqBlock>());
    engine.setChain (std::move (chain));

    rebuildBlockPanels();
}

void MainComponent::restoreLastFiles()
{
    for (auto& panel : blockPanels)
        if (auto* block = panelBlocks[panel.get()])
            if (block->getFileSlot() != nullptr)
                if (const auto file = settings.getLastFile (block->getTypeId()); file.existsAsFile())
                    panel->loadFile (file);
}

void MainComponent::rebuildBlockPanels()
{
    blockPanels.clear();
    shownChain = engine.getChain();

    if (shownChain == nullptr)
        return;

    panelBlocks.clear();

    for (auto& block : shownChain->getBlocks())
    {
        auto panel = std::make_unique<BlockPanel> (*block);

        if (auto* gate = dynamic_cast<NoiseGateBlock*> (block.get()))
            panel->setStatusLight ("open", [gate] { return gate->isOpen(); });

        auto* blockPtr = block.get();
        panel->getBrowseFolder = [this] { return settings.getLastBrowseFolder(); };
        panel->onFileLoaded = [this, blockPtr] (const juce::File& file)
        {
            settings.setLastFile (blockPtr->getTypeId(), file);
            settings.setLastBrowseFolder (file.getParentDirectory());
            refreshStatus();   // latency may have changed
        };

        addAndMakeVisible (*panel);
        panelBlocks[panel.get()] = blockPtr;
        blockPanels.push_back (std::move (panel));
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
    systemStatus.setColour (juce::Label::textColourId,
                            problem ? juce::Colour (0xfff0b429) : juce::Colours::white.withAlpha (0.55f));
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
    const auto chainLatency = shownChain != nullptr ? shownChain->getLatencySamples() : 0;
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
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
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

    if (engine.getChain() != shownChain)
        rebuildBlockPanels();

    inputMeter.setPeak (engine.getInputMeter().take());
    outputMeter.setPeak (engine.getOutputMeter().take());

    for (auto& panel : blockPanels)
        panel->refresh();

    // Text changes ~3x a second so numbers stay readable.
    if (++frameCounter % 10 != 0)
        return;

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
    constexpr int margin = 24, gap = 16;
    auto area = juce::Rectangle<int> (0, 0, width, 100000).reduced (margin);
    const auto place = [apply] (juce::Component& c, juce::Rectangle<int> r) { if (apply) c.setBounds (r); };

    auto top = area.removeFromTop (44);
    place (audioSettingsButton, top.removeFromRight (180).reduced (0, 4));
    place (title, top);

    area.removeFromTop (12);
    place (deviceStatus, area.removeFromTop (26));
    place (latencyStatus, area.removeFromTop (26));
    place (systemStatus, area.removeFromTop (22));
    place (warning, area.removeFromTop (26));

    area.removeFromTop (12);
    auto inputRow = area.removeFromTop (36);
    place (inputLabel, inputRow.removeFromLeft (140));
    place (inputChoice, inputRow.removeFromLeft (320).reduced (0, 2));

    area.removeFromTop (20);
    place (inputMeter, area.removeFromTop (34));
    area.removeFromTop (10);
    place (outputMeter, area.removeFromTop (34));

    area.removeFromTop (20);
    auto volumeRow = area.removeFromTop (64);
    place (muteButton, volumeRow.removeFromRight (160));
    volumeRow.removeFromRight (16);
    place (volumeLabel, volumeRow.removeFromLeft (140));
    place (volume, volumeRow);

    area.removeFromTop (16);
    auto healthRow = area.removeFromTop (32);
    place (resetCountersButton, healthRow.removeFromRight (160));
    place (healthStatus, healthRow);

    // The chain: panels left to right, wrapping onto new rows as needed.
    area.removeFromTop (20);
    place (chainLabel, area.removeFromTop (26));
    area.removeFromTop (6);

    int x = area.getX(), y = area.getY(), rowHeight = 0;

    for (auto& panel : blockPanels)
    {
        const auto w = juce::jmin (panel->getPreferredWidth(), area.getWidth());
        const auto h = panel->getPreferredHeight();

        if (x > area.getX() && x + w > area.getRight())
        {
            x = area.getX();
            y += rowHeight + gap;
            rowHeight = 0;
        }

        place (*panel, { x, y, w, h });
        x += w + gap;
        rowHeight = juce::jmax (rowHeight, h);
    }

    return y + rowHeight + margin;
}

} // namespace encore
