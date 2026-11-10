// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "engine/core/Block.h"

namespace encore
{

/** The editor for one block: name, on/off switch, CPU use and a knob per
    parameter, built automatically from the block's parameter list. The same
    panel becomes the parameter area of the edit view in milestone 4.

    Message thread only. It reads and writes the block's atomics, so it never
    waits on the audio thread.
*/
class BlockPanel final : public juce::Component
{
public:
    explicit BlockPanel (Block& blockToEdit);
    ~BlockPanel() override;

    /** Optional activity light (e.g. "gate open"). */
    void setStatusLight (juce::String label, std::function<bool()> isLit);

    /** Call from a UI timer: syncs knobs changed elsewhere (MIDI later) and the CPU readout. */
    void refresh();

    int getPreferredWidth() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        explicit Knob (Parameter& p) : parameter (p) {}

        Parameter& parameter;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
    };

    Block& block;
    juce::Label title, cpu;
    juce::TextButton power { "ON" };
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::String statusLabel;
    std::function<bool()> statusLit;
    bool statusWasLit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockPanel)
};

} // namespace encore
