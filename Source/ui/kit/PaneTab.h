// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A small tab on a pane's top edge that opens and closes a view of it
    (V3.2-CHANGES §3.1, the mixer view): 44 × 12, the pane's fill, its
    border on three sides (none at the bottom, so it joins the pane), top
    corners 6; a 10 px chevron, up while closed, down while open. The owner
    places it centred on the pane, 7 px above its top, and flips it in
    onClick. */
class PaneTab final : public juce::Button
{
public:
    PaneTab();

    void setOpen (bool);
    bool isOpen() const noexcept { return open; }

    /** Tooltips while closed and while open. */
    void setTooltips (const juce::String& whenClosed, const juce::String& whenOpen);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void updateTooltip();

    bool open = false;
    juce::String closedTip, openTip;
    juce::Path outline, filled;   // made on resize
    void resized() override;
};

} // namespace encore::kit
