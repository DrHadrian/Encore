// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/PaneTab.h"

#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

PaneTab::PaneTab() : juce::Button ({})
{
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PaneTab::setOpen (bool shouldBeOpen)
{
    if (shouldBeOpen != open)
    {
        open = shouldBeOpen;
        updateTooltip();
        repaint();
    }
}

void PaneTab::setTooltips (const juce::String& whenClosed, const juce::String& whenOpen)
{
    closedTip = whenClosed;
    openTip = whenOpen;
    updateTooltip();
}

void PaneTab::updateTooltip()
{
    setTooltip (open ? openTip : closedTip);
    setTitle (open ? openTip : closedTip);
}

void PaneTab::resized()
{
    // Three sides and the two top corners; open at the bottom (it joins the pane there).
    const auto r = getLocalBounds().toFloat().reduced (0.5f, 0.0f).withTrimmedTop (0.5f);
    const auto rad = juce::jmin ((float) size::paneTabRadius, r.getHeight());
    outline.clear();
    outline.startNewSubPath (r.getX(), r.getBottom());
    outline.lineTo (r.getX(), r.getY() + rad);
    outline.quadraticTo (r.getX(), r.getY(), r.getX() + rad, r.getY());
    outline.lineTo (r.getRight() - rad, r.getY());
    outline.quadraticTo (r.getRight(), r.getY(), r.getRight(), r.getY() + rad);
    outline.lineTo (r.getRight(), r.getBottom());
    filled = outline;
    filled.closeSubPath();
}

void PaneTab::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    g.setColour (paint::c (colour::pane));
    g.fillPath (filled);
    g.setColour (paint::c (colour::paneBorder));
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    const auto chevron = (float) size::paneTabChevron;
    drawIcon (g, open ? Icon::chevronDown : Icon::chevronUp,
              getLocalBounds().toFloat().withTrimmedTop (1.0f).withSizeKeepingCentre (chevron, chevron),
              paint::c (highlighted ? colour::textStrong : colour::textTertiary));

    if (hasKeyboardFocus (false) && getWantsKeyboardFocus())
        paint::focusRing (g, getLocalBounds().toFloat(), (float) size::paneTabRadius);
}

} // namespace encore::kit
