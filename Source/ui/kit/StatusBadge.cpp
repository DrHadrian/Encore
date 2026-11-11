// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/StatusBadge.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    juce::Colour fillFor (Status status)
    {
        return status == Status::problem ? paint::c (colour::problemBadge)
             : status == Status::warning ? paint::c (colour::amberStrong)
                                         : paint::c (colour::okBadgeFill);
    }
}

void paintStatusBadge (juce::Graphics& g, juce::Rectangle<float> bounds, Status status)
{
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto r = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto scale = side / (float) size::statusBadge;

    g.setColour (fillFor (status));
    g.fillEllipse (r);

    if (status == Status::problem)
        paint::cross (g, r.reduced (side * 0.2f), paint::c (colour::textStrong), 2.0f * scale);
    else if (status == Status::warning)
        paint::text (g, "!", r, type::statusMark, colour::amberInk, juce::Justification::centred, scale);
    else
        paint::tick (g, r.reduced (side * 0.22f), paint::c (colour::okBadgeTick), 2.0f * scale);
}

SummaryBadge::SummaryBadge()
{
    setInterceptsMouseClicks (false, false);
}

int SummaryBadge::getIdealSize() noexcept
{
    return size::statusBadgeLarge + size::statusGlow * 2;
}

void SummaryBadge::setStatus (Status s)
{
    if (s != status)
    {
        status = s;
        repaint();
    }
}

void SummaryBadge::paint (juce::Graphics& g)
{
    // The whole badge (glow, dome, mark) is rendered into an image at the
    // screen's scale when the status, size or scale changes; paint() only
    // draws that image.
    const auto bounds = getLocalBounds().toFloat();
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto w = juce::jmax (1, juce::roundToInt (bounds.getWidth() * scale));
    const auto h = juce::jmax (1, juce::roundToInt (bounds.getHeight() * scale));

    if (glow.isNull() || glowStatus != status || ! juce::approximatelyEqual (scale, glowScale)
        || glow.getWidth() != w || glow.getHeight() != h)
    {
        glowScale = scale;
        glowStatus = status;
        glow = juce::Image (juce::Image::ARGB, w, h, true);

        juce::Graphics ig (glow);
        ig.addTransform (juce::AffineTransform::scale ((float) w / juce::jmax (1.0f, bounds.getWidth()),
                                                       (float) h / juce::jmax (1.0f, bounds.getHeight())));

        const auto side = juce::jmin ((float) size::statusBadgeLarge, juce::jmin (bounds.getWidth(), bounds.getHeight()));
        const auto r = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
        const auto fill = fillFor (status);
        const auto light = status == Status::problem ? paint::c (colour::problemBadgeHi) : fill.brighter (state::badgeLight);
        const auto rim = status == Status::problem ? paint::c (colour::problemBadgeLo) : fill.darker (state::badgeRim);

        // box-shadow: 0 0 18px fill at 45 %
        juce::Path circle;
        circle.addEllipse (r);
        const auto glowColour = status == Status::problem ? paint::c (colour::problemGlow) : fill.withAlpha (state::badgeGlowAlpha);
        juce::DropShadow (glowColour, size::statusGlow, {}).drawForPath (ig, circle);

        // radial-gradient (circle at 35 % 30 %, light, fill 60 %, rim)
        const auto centre = r.getTopLeft() + juce::Point<float> (side * 0.35f, side * 0.30f);
        const auto reach = centre.getDistanceFrom (r.getBottomRight());
        juce::ColourGradient dome (light, centre, rim, centre + juce::Point<float> (reach, 0.0f), true);
        dome.addColour (0.6, fill);
        ig.setGradientFill (dome);
        ig.fillEllipse (r);

        const auto markScale = side / (float) size::statusBadgeLarge;

        if (status == Status::ok)
            paint::tick (ig, r.reduced (side * 0.22f), paint::c (colour::okBadgeTick), 4.0f * markScale);
        else
            paint::text (ig, "!", r, type::statusMarkLarge, status == Status::problem ? colour::textStrong : colour::amberInk,
                         juce::Justification::centred, markScale);
    }

    g.drawImage (glow, bounds);
}

} // namespace encore::kit
