// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/kit/LevelBars.h"
#include "ui/kit/OutputStrip.h"
#include "ui/kit/SegmentedControl.h"

namespace encore
{

/** The bottom row's left pane (NEW-FEATURES-M10-M11 §B.4): "Input", the
    playing patch's input name and channel, its meter. A name that isn't on
    this computer shows what plays instead, in amber. Paints only. */
class InputSummary final : public juce::Component
{
public:
    InputSummary();

    /** `missing`: the patch asks for a name not here; `name` is what plays. */
    void setInput (const juce::String& name, const juce::String& caption, bool missing);
    void setPeak (float linear) { meter.setPeaks (linear, 0.0f); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name, caption;
    bool missing = false;
    float nameWidth = 0.0f;
    kit::LevelBars meter { kit::LevelBars::Orientation::horizontal, 1 };
};

/** The bottom row's centre pane: "OUTPUTS", how many, Compact | Full, and a
    strip per output of this computer (kit::OutputStrip). Compact strips
    share the width; full ones are 170 wide and the row scrolls sideways. */
class OutputsPane final : public juce::Component
{
public:
    struct Output
    {
        juce::String name, channels, levelText;
        bool stereo = true, muted = false;

        bool operator== (const Output&) const = default;
    };

    OutputsPane();

    /** The outputs in order; strips are rebuilt only when the names change. */
    void setOutputs (const std::vector<Output>&);
    void setFull (bool full);
    bool isFull() const noexcept { return full; }

    /** UI timer: an output's peaks (index in setOutputs' order). */
    void setPeaks (int index, float left, float right);

    std::function<void (int index, bool muted)> onMuteClicked;
    std::function<void (bool full)> onFullChanged;

    int getNumStrips() const noexcept { return strips.size(); }
    kit::OutputStrip* getStrip (int index) const noexcept { return strips[index]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<Output> outputs;
    juce::String note;
    bool full = false;
    kit::SegmentedControl mode;
    juce::Viewport viewport;
    juce::Component row;
    juce::OwnedArray<kit::OutputStrip> strips;
};

} // namespace encore
