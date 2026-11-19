// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/kit/DialogFrame.h"
#include "ui/kit/LevelBars.h"
#include "ui/kit/MixerStrip.h"
#include "ui/kit/OutputStrip.h"
#include "ui/kit/PopupField.h"

namespace encore
{

/** The bottom row's left pane (V3.2-CHANGES §3): the playing patch's input.
    Collapsed: "Input", the picker ("Guitar · 1 · Input 1 (Mic/Inst)") and an
    8 px meter with its peak mark. The mixer view: kit::InputStrip. A name
    the patch asks for that isn't on this computer shows in amber. The owner
    shows the menu of inputs in onPickInput (anchored to the picker). */
class InputSummary final : public juce::Component
{
public:
    InputSummary();

    void setInput (const juce::String& name, const juce::String& caption, bool missing);
    void setPeak (float linear);
    void setMixer (bool);
    bool isMixer() const noexcept { return mixer; }

    /** The pane's width in the mixer view (collapsed: the sidebar's). */
    static int mixerWidth() noexcept;

    std::function<void (juce::Component& anchor)> onPickInput;

    kit::PopupField& getPicker() noexcept { return mixer ? strip.getPicker() : picker; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    bool mixer = false;
    kit::PopupField picker;
    kit::LevelBars meter { kit::LevelBars::Orientation::horizontal, 1 };
    kit::InputStrip strip;
    juce::Rectangle<float> labelArea;
};

/** The bottom row's centre pane: one strip per output of this computer.
    Collapsed (kit::OutputStrip) they share the width with 1 px dividers;
    in the mixer view (kit::MixerStrip) each keeps its width, the rest of the
    pane stays empty, and a row wider than the pane scrolls sideways with the
    wheel. Strips are rebuilt only when the outputs' names change. */
class OutputsPane final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Output
    {
        juce::String name, channels, levelText;
        float levelDb = 0.0f;
        bool stereo = true, muted = false;
    };

    OutputsPane();
    ~OutputsPane() override;

    void setOutputs (const std::vector<Output>&);
    void setMixer (bool);
    bool isMixer() const noexcept { return mixer; }

    /** UI timer: an output's peaks and limiter reduction (index in setOutputs' order). */
    void setPeaks (int index, float left, float right);
    void setLimiterReduction (int index, float dB);

    std::function<void (int index, bool muted)> onMuteClicked;
    std::function<void (int index, float dB)> onLevelChange;   // while a fader moves
    std::function<void (int index, float dB)> onLevelCommit;   // when it lets go
    std::function<void (int index)> onPeakReset;

    int getNumStrips() const noexcept { return strips.size(); }
    kit::OutputStrip* getStrip (int index) const noexcept { return strips[index]; }
    kit::MixerStrip* getMixerStrip (int index) const noexcept { return mixerStrips[index]; }
    juce::Viewport& getViewport() noexcept { return viewport; }

    void resized() override;

private:
    class Row;

    void rebuild();
    void layoutRow();

    std::vector<Output> outputs;
    bool mixer = false;
    juce::Viewport viewport;
    std::unique_ptr<Row> row;
    juce::OwnedArray<kit::OutputStrip> strips;
    juce::OwnedArray<kit::MixerStrip> mixerStrips;
};

/** System health in full (the health status's click, Edit only): today's
    lines (CPU and counters, latency, device, then each thing to check, in
    amber), and Reset counters. The owner refreshes the lines while it's
    open. */
class HealthDetailsDialog final
{
public:
    struct Line
    {
        juce::String text, tooltip;
        bool problem = false;
    };

    HealthDetailsDialog (std::function<void()> resetCounters, std::function<void()> close);
    ~HealthDetailsDialog();

    void setLines (const std::vector<Line>&);

    juce::Component& getOverlay() noexcept { return *overlay; }
    kit::DialogFrame& getFrame() noexcept { return frame; }

private:
    class Content;

    kit::DialogFrame frame;
    std::unique_ptr<Content> content;
    std::unique_ptr<kit::DialogOverlay> overlay;
};

} // namespace encore
