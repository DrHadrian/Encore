// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/HealthStatus.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

HealthStatus::HealthStatus() : juce::Button ({})
{
    setMouseClickGrabsKeyboardFocus (false);
    setStatus ({}, {});
}

void HealthStatus::setStatus (const juce::StringArray& problems, const juce::String& newDetail)
{
    const auto n = problems.size();
    const auto newHeadline = n == 0 ? juce::String ("Healthy")
                           : n == 1 ? juce::String ("1 thing to check")
                                    : juce::String (n) + " things to check";
    const auto tip = (n == 0 ? juce::String() : problems.joinIntoString ("\n") + "\n")
                   + "Click for system health details (CPU, dropouts, device, latency).";

    if (getTooltip() != tip)
        setTooltip (tip);

    if (n != numProblems || newHeadline != headline || newDetail != detail)
    {
        numProblems = n;
        headline = newHeadline;
        detail = newDetail;
        setTitle ("System health: " + headline);
        repaint();
    }
}

void HealthStatus::setBottomAligned (bool b)
{
    if (b != bottomAligned)
    {
        bottomAligned = b;
        repaint();
    }
}

void HealthStatus::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = (float) size::healthStatusRadius;

    if (highlighted || down)
    {
        g.setColour (paint::c (colour::healthHover));
        g.fillRoundedRectangle (r, radius);
    }

    const auto lineH = (float) size::healthLineH;
    const auto blockH = 2.0f * lineH + (float) size::healthLineGap;
    auto area = r.reduced ((float) size::healthStatusPad);
    const auto top = bottomAligned ? area.getBottom() - blockH : area.getCentreY() - blockH * 0.5f;
    auto first = juce::Rectangle<float> (area.getX(), top, area.getWidth(), lineH);
    const auto second = first.translated (0.0f, lineH + (float) size::healthLineGap);

    if (numProblems == 0)
    {
        const auto dot = (float) size::healthDot;
        g.setColour (paint::c (colour::healthy));
        g.fillEllipse (first.removeFromLeft (dot).withSizeKeepingCentre (dot, dot));
        first.removeFromLeft ((float) size::healthDotGap);
        paint::text (g, headline, first, type::healthStatus, colour::textMuted);
    }
    else
    {
        const auto icon = (float) size::healthIcon;
        drawIcon (g, Icon::warn, first.removeFromLeft (icon).withSizeKeepingCentre (icon, icon), paint::c (colour::amber));
        first.removeFromLeft ((float) size::healthIconGap);
        paint::text (g, headline, first, type::healthStatusStrong, colour::amber);
    }

    paint::text (g, detail, second, type::healthStatus, colour::textDim);

    if (hasKeyboardFocus (false) && getWantsKeyboardFocus())
        paint::focusRing (g, r, radius);
}

} // namespace encore::kit
