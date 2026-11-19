// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/ProgressBar.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

ProgressBar::ProgressBar()
{
    setInterceptsMouseClicks (false, false);
}

void ProgressBar::setProgress (double proportion)
{
    const auto p = std::isfinite (proportion) ? juce::jlimit (0.0, 1.0, proportion) : 0.0;

    if (! juce::approximatelyEqual (p, progress))
    {
        progress = p;
        repaint();
    }
}

void ProgressBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) size::progressH);
    const auto radius = (float) size::progressRadius;
    paint::inset (g, r, colour::meterTrack, colour::meterTrack, radius);

    if (progress > 0.0)
    {
        g.setColour (paint::c (colour::accent));
        g.fillRoundedRectangle (r.withWidth (juce::jmax (radius * 2.0f, r.getWidth() * (float) progress)), radius);
    }
}

} // namespace encore::kit
