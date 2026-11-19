// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** Where a dragged thing lands (V3.2-CHANGES §7; the layout editor, the amp
    builder): a dashed 2 px accent box, radius 6, filled with the accent at
    14 %, and a centred line ("Drop: Clock", "Move: Clock") in 12 semibold.
    Refused (no room for it): amber instead, and nothing moves. The owner
    sets its bounds to the snapped place; it repaints only when its text or
    state changes. It never takes clicks. */
class DropBox final : public juce::Component
{
public:
    DropBox();

    void setLabel (const juce::String&);
    void setRefused (bool);

    const juce::String& getLabel() const noexcept { return label; }
    bool isRefused() const noexcept { return refused; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String label;
    bool refused = false;
    juce::Path dashes;   // made on resize
};

} // namespace encore::kit
