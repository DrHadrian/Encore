// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "session/ShowSession.h"
#include "ui/StageView.h"
#include "ui/kit/Chip.h"
#include "ui/kit/SegmentedControl.h"

namespace encore
{

/** The Routing view (NEW-FEATURES-M10-M11 §B.2), opened with F7 or the
    Routing button in Edit: it covers the main grid like the full list. One
    pane with a sub-toolbar (Routing · Inputs & outputs | Sends · THIS
    COMPUTER / THIS SHOW · a summary · Back to patches F7) and a footer.

    - Inputs & outputs: this computer's inputs (name, channel, meter),
      the interface, the names the show uses (here or NOT HERE, with Add),
      its outputs (channels, level, M, meters, EQ, delay) and, for the
      selected output, its EQ, delay and safety limiter.
    - Sends: every patch against every output name, the level each patch
      sends; outputs that aren't here in amber.

    Changes go out through the callbacks (the main window applies routing,
    saves it and refreshes the session); patch sends go to the session.
    Message thread only; refresh() and update() are driven by the main
    window's timer (no timer of its own). */
class RoutingView final : public juce::Component
{
public:
    RoutingView (AudioEngine&, ShowSession&, juce::AudioDeviceManager&);
    ~RoutingView() override;

    enum class Tab { inputsOutputs, sends };
    void setTab (Tab);
    Tab getTab() const noexcept { return tab; }

    /** Inputs & outputs: whose EQ and delay show (an index into the outputs). */
    void selectOutput (int index);
    int getSelectedOutput() const noexcept { return selectedOutput; }

    /** Sends: the selected cell (patch row, output column), -1 for none. */
    void selectSend (int row, int column);

    std::function<void()> onClose;
    std::function<void (const RoutingConfig&)> onSetRouting;
    std::function<void (const juce::String& output, bool muted)> onSetOutputMuted;
    std::function<void()> onOpenAudioSettings;

    /** From the UI timer: rebuilds what changed (routing, the show, the device). */
    void refresh();

    /** From the UI timer: meters, the limiter line. */
    void update (const StageSnapshot&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class IoPage;
    class SendsPage;

    void applyRouting (const RoutingConfig&);
    juce::String summaryText() const;

    AudioEngine& engine;
    ShowSession& session;
    juce::AudioDeviceManager& deviceManager;

    Tab tab = Tab::inputsOutputs;
    int selectedOutput = 0;
    juce::int64 lastState = 0;

    kit::SegmentedControl tabs;
    kit::Chip scopeChip { kit::ChipStyle::plain };
    juce::TextButton backButton { "Back to patches" };
    juce::String summary, footer;

    std::unique_ptr<IoPage> ioPage;
    std::unique_ptr<SendsPage> sendsPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingView)
};

} // namespace encore
