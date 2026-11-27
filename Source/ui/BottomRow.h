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

/** What the System health window shows (board BHealth, v32-README A3). The
    owner builds it from the engine and the device while the window is open. */
struct HealthReport
{
    struct Row
    {
        juce::String label, value;
        bool problem = false;   // the value in amber (a lost device)
        bool operator== (const Row&) const = default;
    };

    struct Check
    {
        juce::String text;
        bool fine = true;       // a green tick; else an amber warning
        bool operator== (const Check&) const = default;
    };

    juce::StringArray toCheck;   // the health status's problems; none: "Healthy"
    juce::String summary;        // "CPU 9% · 5.3 ms round trip"
    std::vector<Row> audio, device;
    std::vector<Check> computer; // empty off Windows: no Computer group

    bool operator== (const HealthReport&) const = default;
};

/** System health in full (the health status's click, Edit only): a summary
    (a badge, "N things to check" or "Healthy", CPU and round trip, Live), the
    things to check, then Audio, Device and Computer, and Reset counters.
    640 x 492; the body scrolls if many things need checking. */
class HealthDetailsDialog final
{
public:
    HealthDetailsDialog (std::function<void()> resetCounters, std::function<void()> close);
    ~HealthDetailsDialog();

    /** Shows `report`; repaints only when it changed. */
    void setReport (const HealthReport&);
    const HealthReport& getReport() const noexcept;

    juce::Component& getOverlay() noexcept { return *overlay; }
    kit::DialogFrame& getFrame() noexcept { return frame; }

private:
    class Content;
    class Body;

    kit::DialogFrame frame;
    std::unique_ptr<Body> body;
    std::unique_ptr<kit::DialogOverlay> overlay;
};

} // namespace encore
