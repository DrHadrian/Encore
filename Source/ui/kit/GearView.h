// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** What a piece of gear shows. Filled by the screen from the block. */
struct BlockInfo
{
    enum class Shape { amp, cab, pedal };

    Shape shape = Shape::pedal;
    juce::String kindLabel;   // "AMP · NAM CAPTURE", "CAB · IR", "GATE", "EQ", "DELAY" (any case)
    juce::String name;        // the block's display name ("Noise gate")
    juce::String plate;       // amp panel model plate ("JC 120B"), empty for none
    bool on = true;

    bool operator== (const BlockInfo&) const = default;
};

/** The contract every amp, cab and pedal drawing keeps (EXTENDING.md §6).
    GearPlaceholder is the first implementation; rendered gear art replaces
    it later without changing any layout: the sizes and the control area are
    fixed by the design. */
class GearView : public juce::Component
{
public:
    enum class Size { chainTile, stagePedal, panel };

    explicit GearView (Size s) : gearSize (s) {}

    Size getGearSize() const noexcept { return gearSize; }

    virtual void setBlock (const BlockInfo&) = 0;

    /** Where the owner puts knobs, toggles and the LED (panel size only;
        the amp's is 22, 44, 556 × 138). Empty for tiles and pedals. */
    virtual juce::Rectangle<int> getControlArea() const = 0;

    /** The LED / lamp (pedals) or the power state shown on the box. */
    virtual void setOn (bool) = 0;

    /** The box for a shape at a size: chain tiles 117 × 84 (amp) or 64 × 84;
        stage pedals 160 × 210 (drawn scaled to the widget); panels 600 × 430
        (amp), 230 × 300 (cab), 600 × 300 (other blocks). */
    static juce::Rectangle<int> idealBounds (Size, BlockInfo::Shape);

private:
    const Size gearSize;
};

} // namespace encore::kit
