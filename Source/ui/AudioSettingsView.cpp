// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/AudioSettingsView.h"

#include "ui/kit/StatusBadge.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    const juce::String& middot()
    {
        static const juce::String s (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        return s;
    }

    juce::String rateText (double rate)
    {
        // "48 000 Hz", as the board writes it.
        const auto hz = juce::roundToInt (rate);
        return (hz >= 1000 ? juce::String (hz / 1000) + " " + juce::String (hz % 1000).paddedLeft ('0', 3) : juce::String (hz)) + " Hz";
    }

    juce::String ms (double milliseconds)  { return juce::String (milliseconds, 1) + " ms"; }

    constexpr int commonSizes[] = { 32, 64, 128, 256, 480 };   // the buffer sizes shown as buttons (SCREENS.md §7)
    constexpr float fullBarRoundTrips = 2.0f, fullBarMinMs = 10.0f;   // the latency bar: twice the round trip, at least 10 ms

    void noFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
    }
}

AudioSettingsView::AudioSettingsView (juce::AudioDeviceManager& dm, const system::TuningReport& report, std::function<void()> reset)
    : deviceManager (dm), tuning (report)
{
    for (auto* group : { &deviceGroup, &channelsGroup, &tuningGroup, &statusGroup, &tipGroup })
        addAndMakeVisible (group);

    driverTypes.onClick = [this] (int index)
    {
        const auto& types = deviceManager.getAvailableDeviceTypes();

        if (juce::isPositiveAndBelow (index, types.size()))
            deviceManager.setCurrentAudioDeviceType (types[index]->getTypeName(), true);

        refresh();
    };

    outputDevice.setTitle ("Device");
    outputDevice.onChange = [this]
    {
        const auto name = outputDevice.getText();
        apply ([this, name] (auto& setup)
        {
            setup.outputDeviceName = name;

            if (! separateInputs)
                setup.inputDeviceName = name;
        });
    };

    inputDevice.setTitle ("Input device");
    inputDevice.onChange = [this] { const auto name = inputDevice.getText(); apply ([name] (auto& setup) { setup.inputDeviceName = name; }); };

    sampleRate.setTitle ("Sample rate");
    sampleRate.onChange = [this]
    {
        const auto rate = (double) sampleRate.getSelectedId();
        apply ([rate] (auto& setup) { setup.sampleRate = rate; });
    };

    bufferSizes.setTitle ("Buffer size");
    bufferSizes.onClick = [this] (int index)
    {
        if (juce::isPositiveAndBelow (index, (int) shownSizes.size()))
        {
            const auto samples = shownSizes[(size_t) index];
            apply ([samples] (auto& setup) { setup.bufferSize = samples; });
        }
    };

    moreSizes.setTitle ("Other buffer sizes");
    moreSizes.setTooltip ("Other buffer sizes the device offers");
    moreSizes.onChange = [this]
    {
        if (const auto samples = moreSizes.getSelectedId(); samples > 0)
            apply ([samples] (auto& setup) { setup.bufferSize = samples; });
    };

    setButtonIcon (controlPanel, Icon::sliders, (float) size::iconSize + 1.0f, false);
    controlPanel.setTooltip ("Opens the driver's own settings window (ASIO drivers often have one for the buffer size and routing)");
    controlPanel.onClick = [this]
    {
        if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->hasControlPanel())
        {
            // As JUCE's selector does: the driver may have changed under us.
            if (device->showControlPanel())
            {
                deviceManager.closeAudioDevice();
                deviceManager.restartLastAudioDevice();
            }

            refresh();
        }
    };

    resetCounters.onClick = [reset = std::move (reset)] { if (reset != nullptr) reset(); };

    for (auto* c : std::initializer_list<juce::Component*> { &driverTypes, &outputDevice, &inputDevice, &sampleRate, &bufferSizes, &moreSizes,
                                                             &controlPanel, &resetCounters })
    {
        noFocus (*c);
        addChildComponent (c);
    }

    driverTypes.setVisible (true);
    resetCounters.setVisible (true);

    deviceManager.addChangeListener (this);
    refresh();
}

AudioSettingsView::~AudioSettingsView()
{
    deviceManager.removeChangeListener (this);
}

void AudioSettingsView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void AudioSettingsView::apply (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    change (setup);
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Error when trying to open audio device!", error);

    refresh();
}

void AudioSettingsView::refresh()
{
    const auto& types = deviceManager.getAvailableDeviceTypes();
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto setup = deviceManager.getAudioDeviceSetup();
    deviceOpen = device != nullptr && device->isOpen();

    // Driver types
    {
        juce::StringArray names;
        int selected = -1;

        for (int i = 0; i < types.size(); ++i)
        {
            names.add (types[i]->getTypeName());

            if (types[i] == type)
                selected = i;
        }

        if (names != juce::StringArray ([&] { juce::StringArray shown;
                                              for (int i = 0; i < driverTypes.getNumSegments(); ++i)
                                                  shown.add (driverTypes.getSegmentButton (i)->getButtonText());
                                              return shown; }()))
        {
            driverTypes.setSegments (names);

            for (int i = 0; i < driverTypes.getNumSegments(); ++i)
                noFocus (*driverTypes.getSegmentButton (i));
        }

        driverTypes.setSelected (selected);
    }

    // Devices
    separateInputs = type != nullptr && type->hasSeparateInputsAndOutputs();
    const auto fill = [] (juce::ComboBox& box, const juce::StringArray& names, const juce::String& current)
    {
        box.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            box.addItem (names[i], i + 1);

        box.setSelectedId (names.indexOf (current) + 1, juce::dontSendNotification);
    };

    fill (outputDevice, type != nullptr ? type->getDeviceNames (false) : juce::StringArray(), setup.outputDeviceName);
    fill (inputDevice, type != nullptr ? type->getDeviceNames (true) : juce::StringArray(), setup.inputDeviceName);
    outputDevice.setVisible (true);
    inputDevice.setVisible (separateInputs);
    controlPanel.setVisible (device != nullptr && device->hasControlPanel());

    // Sample rates
    sampleRate.clear (juce::dontSendNotification);

    if (device != nullptr)
        for (auto rate : device->getAvailableSampleRates())
            sampleRate.addItem (rateText (rate), juce::roundToInt (rate));

    sampleRate.setSelectedId (device != nullptr ? juce::roundToInt (device->getCurrentSampleRate()) : 0, juce::dontSendNotification);
    sampleRate.setVisible (deviceOpen);

    // Buffer sizes: the common ones the device offers (and the current one) as buttons, the rest in a popup.
    shownSizes.clear();
    moreSizes.clear (juce::dontSendNotification);
    const auto current = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    if (device != nullptr)
    {
        const auto offered = device->getAvailableBufferSizes();

        for (auto size : commonSizes)
            if (offered.contains (size))
                shownSizes.push_back (size);

        if (current > 0 && std::find (shownSizes.begin(), shownSizes.end(), current) == shownSizes.end())
            shownSizes.insert (std::upper_bound (shownSizes.begin(), shownSizes.end(), current), current);

        for (auto size : offered)
            if (std::find (shownSizes.begin(), shownSizes.end(), size) == shownSizes.end())
                moreSizes.addItem (juce::String (size) + " samples", size);
    }

    {
        juce::StringArray names;

        for (auto size : shownSizes)
            names.add (juce::String (size));

        bufferSizes.setSegments (names);

        for (int i = 0; i < bufferSizes.getNumSegments(); ++i)
            noFocus (*bufferSizes.getSegmentButton (i));

        const auto at = std::find (shownSizes.begin(), shownSizes.end(), current);
        bufferSizes.setSelected (at != shownSizes.end() ? (int) (at - shownSizes.begin()) : -1);
    }

    bufferSizes.setVisible (deviceOpen && ! shownSizes.empty());
    moreSizes.setVisible (deviceOpen && moreSizes.getNumItems() > 0);
    moreSizes.setTextWhenNothingSelected ("More");

    // Channels: one tick box each (as before: listed singly, the guitar is mono).
    const auto makeChannels = [this] (juce::OwnedArray<juce::ToggleButton>& buttons, const juce::StringArray& names,
                                      const juce::BigInteger& active, bool isInput)
    {
        buttons.clear();

        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new juce::ToggleButton (names[i]));
            b->setToggleState (active[i], juce::dontSendNotification);
            noFocus (*b);
            b->onClick = [this, b, i, isInput]
            {
                const auto on = b->getToggleState();
                apply ([i, on, isInput] (auto& s)
                {
                    auto& channels = isInput ? s.inputChannels : s.outputChannels;

                    // At least one input and one output stay on (as JUCE's selector kept them).
                    if (! on && channels.countNumberOfSetBits() <= 1)
                        return;

                    channels.setBit (i, on);
                    (isInput ? s.useDefaultInputChannels : s.useDefaultOutputChannels) = false;
                });
            };
            addAndMakeVisible (b);
        }
    };

    makeChannels (inputs, device != nullptr ? device->getInputChannelNames() : juce::StringArray(),
                  device != nullptr ? device->getActiveInputChannels() : juce::BigInteger(), true);
    makeChannels (outputs, device != nullptr ? device->getOutputChannelNames() : juce::StringArray(),
                  device != nullptr ? device->getActiveOutputChannels() : juce::BigInteger(), false);

    // Latency and the status title.
    if (deviceOpen)
    {
        const auto rate = juce::jmax (1.0, device->getCurrentSampleRate());
        inMs = (float) (1000.0 * device->getInputLatencyInSamples() / rate);
        outMs = (float) (1000.0 * device->getOutputLatencyInSamples() / rate);
        latencyLead = "In " + ms (inMs) + " + Out " + ms (outMs) + " = ";
        latencyRoundTrip = ms (inMs + outMs) + " round trip";
        latencyTail = middot() + "shown in the main window too";
        statusTitle = device->getTypeName();
        statusLine = device->getName() + middot() + juce::String (rate / 1000.0, juce::roundToInt (rate) % 1000 == 0 ? 0 : 1)
                     + " kHz" + middot() + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
    }
    else
    {
        inMs = outMs = 0.0f;
        latencyLead = "No audio device open";
        latencyRoundTrip = latencyTail = {};
        statusTitle = "No audio device open";
        statusLine = "Pick a driver type and a device";
    }

    resized();
    repaint();
}

void AudioSettingsView::setStatus (const Status& s)
{
    if (! (s == status))
    {
        status = s;
        repaint (statusGroup.getBounds());
    }
}

//==============================================================================
int AudioSettingsView::deviceBoxHeight() const
{
    const auto rows = separateInputs ? 5 : 4;   // type, device (+ input device), rate, buffer
    return 2 * size::groupPad + size::segmentHeight * 2 + size::buttonHeight * (rows - 2) + size::deviceRowGap * rows
           + size::latencyBarH + size::latencyTextGap + juce::roundToInt (font (type::noteText).getHeight());
}

int AudioSettingsView::channelsBoxHeight() const
{
    const auto rows = juce::jmax (1, juce::jmax (inputs.size(), outputs.size()));
    return 2 * size::groupPad + juce::roundToInt (font (type::settingsLabel).getHeight()) + size::checkHeadGap + rows * size::checkRowH - size::checkHeadGap;
}

int AudioSettingsView::getIdealHeight() const
{
    const auto noteH = juce::roundToInt (font (type::noteText).getHeight()) + size::checkHeadGap;
    return size::settingsPadTop + size::groupTitleH + deviceBoxHeight() + size::groupGap + size::groupTitleH + channelsBoxHeight()
           + noteH + size::groupGap + size::groupTitleH + 2 * size::groupPad + 3 * size::tuningRowH - size::tuningRowH / 2 + size::groupGap;
}

void AudioSettingsView::resized()
{
    const auto pad = size::dialogPadX;
    auto left = juce::Rectangle<int> (pad, size::settingsPadTop, size::audioLeftW, getHeight());
    const auto rightX = left.getRight() + size::settingsColumnGap;
    const auto controlX = size::groupPad + size::settingsLabelW + size::settingsLabelGap;

    // DEVICE
    deviceGroup.setBounds (left.removeFromTop (kit::Group::heightFor (deviceBoxHeight())));
    {
        const auto box = deviceGroup.getBoxBounds() + deviceGroup.getPosition();
        auto y = box.getY() + size::groupPad;
        const auto x = box.getX() + controlX;
        const auto rowH = size::buttonHeight + size::buttonDrop;

        driverTypes.setBounds (x, y, juce::jmax (1, driverTypes.getNumSegments()) * size::driverSegmentW + 2, size::segmentHeight);
        y += size::segmentHeight + size::deviceRowGap;
        outputDevice.setBounds (x, y, size::devicePopupW, rowH);
        controlPanel.setBounds (outputDevice.getRight() + size::deviceRowGap, y, controlPanel.getBestWidthForHeight (rowH), rowH);
        y += size::buttonHeight + size::deviceRowGap;

        if (separateInputs)
        {
            inputDevice.setBounds (x, y, size::devicePopupW, rowH);
            y += size::buttonHeight + size::deviceRowGap;
        }

        sampleRate.setBounds (x, y, size::ratePopupW, rowH);
        y += size::buttonHeight + size::deviceRowGap;
        bufferSizes.setBounds (x, y, juce::jmax (1, bufferSizes.getNumSegments()) * size::bufferSegmentW + 2, size::segmentHeight);
        moreSizes.setBounds (bufferSizes.getRight() + size::deviceRowGap, y + 1, size::ratePopupW, rowH);
    }

    // ACTIVE CHANNELS
    left.removeFromTop (size::groupGap);
    channelsGroup.setBounds (left.removeFromTop (kit::Group::heightFor (channelsBoxHeight())));
    {
        const auto box = channelsGroup.getBoxBounds() + channelsGroup.getPosition();
        const auto headH = juce::roundToInt (font (type::settingsLabel).getHeight());
        const auto top = box.getY() + size::groupPad + headH + size::checkHeadGap;
        const auto colW = size::channelColumnX - size::groupPad;

        for (int i = 0; i < inputs.size(); ++i)
            inputs[i]->setBounds (box.getX() + size::groupPad, top + i * size::checkRowH, colW, size::checkRowH);

        for (int i = 0; i < outputs.size(); ++i)
            outputs[i]->setBounds (box.getX() + size::channelColumnX, top + i * size::checkRowH, colW, size::checkRowH);
    }

    // The note under it, then WINDOWS LIVE TUNING
    left.removeFromTop (juce::roundToInt (font (type::noteText).getHeight()) + size::checkHeadGap + size::groupGap);
    tuningGroup.setBounds (left.removeFromTop (kit::Group::heightFor (2 * size::groupPad + 3 * size::tuningRowH - size::tuningRowH / 2)));

    // STATUS and TIP
    auto right = juce::Rectangle<int> (rightX, size::settingsPadTop, size::audioRightW, getHeight());
    statusGroup.setBounds (right.removeFromTop (kit::Group::heightFor (size::statusRowsY + 6 * size::statusRowH + size::deviceRowGap
                                                                       + size::buttonHeight + size::groupPad)));
    {
        const auto box = statusGroup.getBoxBounds() + statusGroup.getPosition();
        resetCounters.setBounds (box.getX() + size::groupPad, box.getY() + size::statusRowsY + 6 * size::statusRowH + size::deviceRowGap / 2,
                                 resetCounters.getBestWidthForHeight (size::buttonHeight + size::buttonDrop), size::buttonHeight + size::buttonDrop);
    }

    right.removeFromTop (size::groupGap);
    tipGroup.setBounds (right.removeFromTop (kit::Group::heightFor (2 * size::groupPad + 3 * size::helpLineH)));
}

void AudioSettingsView::paintOverChildren (juce::Graphics& g)
{
    // The words, made once (nothing is built while painting).
    static const juce::String driverType ("Driver type"), outputDeviceText ("Output device"), deviceText ("Device"),
        inputDeviceText ("Input device"), sampleRateText ("Sample rate"), bufferSizeText ("Buffer size"), latencyText ("Latency"),
        inputsText ("Inputs"), outputsText ("Outputs"), noDevice ("No audio device open"),
        noteLead ("Which input is the guitar is chosen under "), noteStrong ("Guitar input"), noteTail (" in the main window."),
        mutedNote ("Encore always starts muted, and a safety limiter protects the output."),
        tip ("Use 64 samples for the stage. If you hear clicks, go up one step " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))
             + " Encore shows dropouts here and in the System health widget on stage.");
    static const juce::String tuningItems[] = { "Power throttling off", "High process priority", "Pro Audio audio thread",
                                                "Computer and screen kept awake" };
    static const juce::String statusKeys[] = { "CPU", "Overruns", "Driver dropouts", "Limiter", "Memory", "Latency" };
    const auto labelW = (float) size::settingsLabelW;
    const auto labelFor = [&] (const juce::String& text, const juce::Component& control)
    {
        if (! control.isVisible())
            return;

        const auto box = deviceGroup.getBoxBounds() + deviceGroup.getPosition();
        paint::text (g, text, juce::Rectangle<float> ((float) (box.getX() + size::groupPad), (float) control.getY(), labelW,
                                                      (float) (control.getHeight() - (control.getHeight() > size::segmentHeight - 1 ? 0 : 1))),
                     type::settingsLabel, colour::textSecondary, juce::Justification::centredRight);
    };

    labelFor (driverType, driverTypes);
    labelFor (separateInputs ? outputDeviceText : deviceText, outputDevice);
    labelFor (inputDeviceText, inputDevice);
    labelFor (sampleRateText, sampleRate);
    labelFor (bufferSizeText, bufferSizes);

    // Latency: a bar (in, out) and the line under it, the round trip in green.
    {
        const auto box = deviceGroup.getBoxBounds() + deviceGroup.getPosition();
        const auto x = (float) (box.getX() + size::groupPad + size::settingsLabelW + size::settingsLabelGap);
        const auto barY = (float) (box.getBottom() - size::groupPad) - font (type::noteText).getHeight() - (float) (size::latencyTextGap + size::latencyBarH);
        const juce::Rectangle<float> bar (x, barY, (float) size::devicePopupW, (float) size::latencyBarH);
        const auto lineY = bar.getBottom() + (float) size::latencyTextGap;
        paint::text (g, latencyText, juce::Rectangle<float> ((float) (box.getX() + size::groupPad), barY, labelW, lineY + font (type::noteText).getHeight() - barY),
                     type::settingsLabel, colour::textSecondary, juce::Justification::centredRight);

        g.setColour (paint::c (colour::meterTrack));
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);

        if (inMs + outMs > 0.0f)
        {
            const auto full = juce::jmax (fullBarMinMs, (inMs + outMs) * fullBarRoundTrips);
            const auto inW = bar.getWidth() * inMs / full, outW = bar.getWidth() * outMs / full;
            g.setColour (paint::c (colour::accent));
            g.fillRect (bar.withWidth (inW));
            g.setColour (paint::c (colour::latencyOut));
            g.fillRect (bar.withX (bar.getX() + inW + (float) size::latencySplit).withWidth (juce::jmax (0.0f, outW - (float) size::latencySplit)));
        }

        auto line = juce::Rectangle<float> (x, lineY, (float) (box.getRight() - size::groupPad) - x, font (type::noteText).getHeight());
        const auto run = [&] (const juce::String& text, const type::Style& style, juce::uint32 ink)
        {
            const auto w = textWidth (font (style), text);
            paint::text (g, text, line.removeFromLeft (juce::jmin (w + 1.0f, line.getWidth())), style, ink);
        };

        run (latencyLead, type::noteText, colour::textSoft);
        run (latencyRoundTrip, type::noteStrong, colour::healthy);
        run (latencyTail, type::noteText, colour::textSoft);
    }

    // Channels: column heads; the note under the group.
    {
        const auto box = channelsGroup.getBoxBounds() + channelsGroup.getPosition();
        const auto headH = font (type::settingsLabel).getHeight();
        paint::text (g, inputsText, juce::Rectangle<float> ((float) (box.getX() + size::groupPad), (float) (box.getY() + size::groupPad), (float) (size::channelColumnX - size::groupPad), headH),
                     type::settingsLabel, colour::textSecondary);
        paint::text (g, outputsText, juce::Rectangle<float> ((float) (box.getX() + size::channelColumnX), (float) (box.getY() + size::groupPad), (float) (size::channelColumnX - size::groupPad), headH),
                     type::settingsLabel, colour::textSecondary);

        if (inputs.isEmpty() && outputs.isEmpty())
            paint::text (g, noDevice, juce::Rectangle<float> ((float) (box.getX() + size::groupPad), (float) (box.getY() + size::groupPad) + headH,
                                                                          (float) box.getWidth(), (float) size::checkRowH),
                         type::body, colour::textTertiary);

        auto note = juce::Rectangle<float> ((float) box.getX(), (float) box.getBottom() + (float) size::checkHeadGap * 0.5f,
                                            (float) box.getWidth(), font (type::noteText).getHeight());
        const auto run = [&] (const juce::String& text, const type::Style& style, juce::uint32 ink)
        {
            const auto w = textWidth (font (style), text);
            paint::text (g, text, note.removeFromLeft (juce::jmin (w + 1.0f, note.getWidth())), style, ink);
        };

        run (noteLead, type::noteText, colour::textTertiary);
        run (noteStrong, type::noteStrong, colour::textSoft);
        run (noteTail, type::noteText, colour::textTertiary);
    }

    // Windows live tuning: what applyLiveProcessSettings() managed, ✓ or ✕.
    {
        const auto box = tuningGroup.getBoxBounds() + tuningGroup.getPosition();
        const bool done[] = { tuning.powerThrottlingDisabled, tuning.highPriority, system::isAudioThreadProAudio(), tuning.keepAwake };

        for (int i = 0; i < 4; ++i)
        {
            const auto x = (float) (box.getX() + (i % 2 == 0 ? size::groupPad : size::channelColumnX));
            const auto y = (float) (box.getY() + size::groupPad / 2 + (i / 2) * size::tuningRowH);
            const juce::Rectangle<float> row (x, y, (float) (size::channelColumnX - size::groupPad), (float) size::tuningRowH);
            const auto mark = row.withWidth ((float) size::tickSize).withSizeKeepingCentre ((float) size::tickSize, (float) size::tickSize);

            if (done[i])
                paint::tick (g, mark, paint::c (colour::healthy), size::tickStroke);
            else
                paint::cross (g, mark, paint::c (colour::danger), size::tickStroke);

            paint::text (g, tuningItems[i], row.withTrimmedLeft ((float) (size::tickSize + size::tickTextGap)), type::body, colour::text);
        }

        paint::text (g, mutedNote,
                     juce::Rectangle<float> ((float) (box.getX() + size::groupPad), (float) (box.getY() + size::groupPad / 2 + 2 * size::tuningRowH),
                                             (float) (box.getWidth() - 2 * size::groupPad), (float) size::tuningRowH),
                     type::body, colour::textSecondary);
    }

    // Status: badge, driver and device, then the health values.
    {
        const auto box = (statusGroup.getBoxBounds() + statusGroup.getPosition()).toFloat();
        const auto badge = juce::Rectangle<float> (box.getX() + (float) size::groupPad, box.getY() + (float) size::groupPad + (float) size::latencySplit,
                                                   (float) size::statusBadgeMid, (float) size::statusBadgeMid);
        kit::paintStatusBadge (g, badge, deviceOpen ? kit::Status::ok : kit::Status::problem);
        const auto textX = badge.getRight() + (float) size::deviceRowGap;
        paint::text (g, statusTitle, { textX, box.getY() + (float) size::groupPad, box.getRight() - (float) size::groupPad - textX, (float) size::statusTitleH },
                     type::statusTitle, colour::text);
        paint::text (g, statusLine, { textX, box.getY() + (float) (size::groupPad + size::statusTitleH), box.getRight() - (float) size::groupPad - textX, (float) size::statusLineH },
                     type::noteText, colour::textTertiary);

        g.setColour (paint::c (colour::paneHeaderLine));
        paint::hline (g, box.getX() + (float) size::groupPad, box.getY() + (float) size::statusDividerY, box.getWidth() - 2.0f * (float) size::groupPad);

        const juce::String* values[] = { &status.cpu, &status.overruns, &status.driverDropouts, &status.limiter, &status.memory, &status.latency };

        for (int i = 0; i < 6; ++i)
        {
            const juce::Rectangle<float> row (box.getX() + (float) size::groupPad, box.getY() + (float) (size::statusRowsY + i * size::statusRowH),
                                              box.getWidth() - 2.0f * (float) size::groupPad, (float) size::statusRowH);
            paint::text (g, statusKeys[i], row.withWidth ((float) size::statusKeyW).withHeight ((float) size::statusTextH), type::settingsLabel, colour::textSecondary);
            paint::text (g, *values[i], row.withTrimmedLeft ((float) size::statusKeyW).withHeight ((float) size::statusTextH), type::settingsLabel, colour::text);
        }
    }

    // Tip
    {
        const auto box = (tipGroup.getBoxBounds() + tipGroup.getPosition()).reduced (size::groupPad);
        g.setColour (paint::c (colour::textSecondary));
        g.setFont (font (type::helpText));
        g.drawFittedText (tip, box, juce::Justification::topLeft, 3, 1.0f);
    }
}

} // namespace encore
