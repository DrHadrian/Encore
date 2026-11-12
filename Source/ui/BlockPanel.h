// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/core/Block.h"
#include "model/MidiMapping.h"
#include "ui/kit/FaultCard.h"
#include "ui/kit/GearPlaceholder.h"
#include "ui/kit/KnobView.h"
#include "ui/kit/Pane.h"

namespace encore
{

/** The block stage (docs/design/SCREENS.md §4.3): the selected block as a
    piece of gear. The header shows whether the block is on (dot), what it
    is ("AMP · <model>"), its CPU and the file button ("Load amp...").
    Below, on a lit stage, the block's placeholder panel with a knob per
    parameter and a switch per on/off parameter, built automatically from
    the block's parameter list, the power LED (turns the block on and off)
    and, for the amp, the cab beside it. The whole panel shrinks to fit a
    small window; it never grows past its design size.

    Message thread only. It reads and writes the block's atomics, so it never
    waits on the audio thread.
*/
class BlockPanel final : public kit::Pane
{
public:
    explicit BlockPanel (Block& blockToEdit);
    ~BlockPanel() override;

    /** Optional activity light (e.g. "gate open"). */
    void setStatusLight (juce::String text, std::function<bool()> isLit);

    /** Shows this cab beside the amp (the block after it); clicking it calls onCabClicked. */
    void setCabBeside (const Block* cab);
    std::function<void()> onCabClicked;

    /** Called after any change made in this panel (knob, switch, on/off, file). */
    std::function<void()> onEdited;

    /** Where the file chooser opens, and what to do after a successful load. */
    std::function<juce::File()> getBrowseFolder;
    std::function<void (const juce::File&)> onFileLoaded;

    /** Expression pedals: which pedal moves a knob (0 = none), and what to do
        when one is chosen from the knob's right-click menu. */
    std::function<int (const juce::String& paramId)> getExpressionPedal;
    std::function<void (const juce::String& paramId, int pedal)> onExpressionChosen;

    /** Plugin blocks: "Open plugin window..." in the header (and on the fault card). */
    std::function<void()> onOpenPluginWindow;

    /** Plugin blocks switched off: "Turn back on" / "Load again" on the fault card. */
    std::function<void()> onTurnPluginBackOn;

    /** Loads a file into the block as if picked in the chooser. */
    void loadFile (const juce::File& file);

    /** Call from a UI timer: syncs controls changed elsewhere (MIDI), CPU and file info. */
    void refresh();

    Block& getBlock() const noexcept { return block; }

    /** The scale the gear is drawn at (1 = design size), for tests. */
    float getGearScale() const noexcept { return gearScale; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        explicit Control (Parameter& p) : parameter (p) {}

        Parameter& parameter;
        bool isSwitch = false;
        std::unique_ptr<kit::KnobView> knob;
        std::unique_ptr<kit::GearToggle> toggle;
        int shownPedal = -1;
    };

    /** Right-click on a knob: the expression pedal menu. */
    struct KnobMenu final : public juce::MouseListener
    {
        KnobMenu (BlockPanel& p, Control& c) : panel (p), control (c) {}
        void mouseDown (const juce::MouseEvent& e) override;

        BlockPanel& panel;
        Control& control;
    };

    class Gear;
    class CabColumn;

    void chooseFile();
    void setLoading (bool isLoading);
    void updateHeader();
    juce::uint32 headerDot() const;
    void layOutGear();
    void updateFaultCard();
    juce::String headerLabel() const;

    Block& block;
    juce::TextButton loadButton;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<KnobMenu>> knobMenus;
    std::unique_ptr<Gear> gear;
    std::unique_ptr<CabColumn> cabColumn;
    float gearScale = 1.0f;

    std::unique_ptr<juce::FileChooser> chooser;
    bool loading = false;
    bool shownProblem = false;   // a plugin not loaded or switched off by the guard
    int shownFault = -1;         // the fault the card shows (-1: none yet)
    juce::String shownFile, shownInfo, shownCpu;

    juce::String statusLabel, statusLabelCaps;
    std::function<bool()> statusLit;
    bool statusWasLit = false;

    // The lit stage behind the gear, drawn once per size and screen scale.
    juce::Image stageImage;
    juce::Path stageClip;
    float stageScale = 0.0f;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BlockPanel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockPanel)
};

} // namespace encore
