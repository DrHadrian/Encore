// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A group box in a dialog (DESIGN-SYSTEM.md §5.4): its title in capitals
    above it, then the box (fill #1A1A1D, border, radius 7). The owner adds
    the children and places them in getBoxBounds(); setTitleExtra() keeps
    room on the title line for a control (the MIDI tab's "This show..."). */
class Group final : public juce::Component
{
public:
    explicit Group (const juce::String& title);

    void setGroupTitle (const juce::String& newTitle);

    juce::Rectangle<int> getTitleBounds() const;
    juce::Rectangle<int> getBoxBounds() const;

    /** The group's height for a box this tall. */
    static int heightFor (int boxHeight) noexcept;

    void paint (juce::Graphics&) override;

private:
    juce::String title;   // in capitals
};

} // namespace encore::kit
