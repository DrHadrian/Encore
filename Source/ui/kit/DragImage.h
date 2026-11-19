// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The picture of a thing being dragged, under the pointer (what
    DragAndDropContainer's drag image does): a snapshot of the dragged row,
    taken once when the drag starts, drawn at state::dragImageAlpha and
    moved with the pointer, keeping the point where it was grabbed under it.
    The owner adds it to the component that spans where the drag can go and
    keeps it in front; it may hide it where something else shows the drop
    (the layout editor's drop box). It never takes clicks and repaints only when it moves. */
class DragImage final : public juce::Component
{
public:
    DragImage();

    /** Starts showing `source`'s `area` (in source's coordinates), grabbed at
        `grab` (also in source's coordinates). */
    void start (juce::Component& source, juce::Rectangle<int> area, juce::Point<int> grab);

    /** Moves it so the grabbed point is at `screen`. */
    void moveTo (juce::Point<int> screen);

    void stop();

    bool isDragging() const noexcept { return image.isValid(); }

    void paint (juce::Graphics&) override;

private:
    juce::Image image;
    juce::Point<int> offset;   // grab point from the image's top left
};

} // namespace encore::kit
