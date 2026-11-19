// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** Buttons joined in one rounded group (DESIGN-SYSTEM.md §5.2): the mode
    switch (Stage | Edit | Layout), text sizes, buffer sizes, driver types.
    The selected segment has the pressed look. Clicking a segment calls
    onClick (index); the owner decides whether it becomes the selection. */
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl() = default;

    void setSegments (const juce::StringArray& labels);
    void setTooltips (const juce::StringArray& tooltips);
    void setSelected (int index);   // -1: none

    /** Segment widths in px (as many as segments); empty = equal shares. */
    void setSegmentWidths (std::vector<int>);
    int getSelected() const noexcept { return selected; }
    int getNumSegments() const noexcept { return buttons.size(); }
    juce::TextButton* getSegmentButton (int index) { return buttons[index]; }

    std::function<void (int index)> onClick;

    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::vector<int> widths;
    int selected = -1;
};

} // namespace encore::kit
