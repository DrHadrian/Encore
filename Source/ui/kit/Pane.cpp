// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Pane.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

void Pane::setHeader (const juce::String& newLabel, const juce::String& newRightText)
{
    if (headerHeight == 0)
    {
        headerHeight = size::paneHeader;
        updateHeaderShape();
    }

    const auto shown = paint::caps (type::paneHeader, newLabel);

    if (shown != label || newRightText != rightText)
    {
        label = shown;
        rightText = newRightText;
        repaint (0, 0, getWidth(), headerHeight);
    }
}

void Pane::setHeaderRightText (const juce::String& newRightText)
{
    if (newRightText != rightText)
    {
        rightText = newRightText;
        repaint (0, 0, getWidth(), headerHeight);
    }
}

void Pane::setHeaderHeight (int height)
{
    if (height != headerHeight)
    {
        headerHeight = height;
        updateHeaderShape();
        repaint();
    }
}

void Pane::resized()
{
    updateHeaderShape();
}

void Pane::updateHeaderShape()
{
    headerShape.clear();

    if (headerHeight <= 0)
        return;

    const auto header = juce::Rectangle<float> (1.0f, 1.0f, (float) getWidth() - 2.0f, (float) headerHeight + 1.0f);
    const auto radius = (float) size::paneRadius - 1.0f;
    headerShape.addRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight(),
                                     radius, radius, true, true, false, false);
    headerFill = juce::ColourGradient::vertical (paint::c (colour::paneHeaderTop), header.getY(),
                                                 paint::c (colour::paneHeaderBot), header.getBottom());
}

juce::Rectangle<int> Pane::getHeaderBounds() const
{
    // The gradient part of the header; the 1 px line sits under it.
    return { 1, 1, juce::jmax (0, getWidth() - 2), headerHeight };
}

juce::Rectangle<int> Pane::getContentBounds() const
{
    return getLocalBounds().reduced (1).withTrimmedTop (headerHeight > 0 ? headerHeight + 1 : 0);
}

void Pane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    paint::pane (g, bounds);

    if (headerHeight > 0)
    {
        // Gradient (headerHeight) plus a 1 px line under it, inside the border.
        g.setGradientFill (headerFill);
        g.fillPath (headerShape);
        g.setColour (paint::c (colour::paneHeaderLine));
        paint::hline (g, 1.0f, (float) headerHeight + 1.0f, bounds.getWidth() - 2.0f);

        const auto text = getHeaderBounds().toFloat().withTrimmedLeft ((float) size::paneHeaderPadL)
                                                     .withTrimmedRight ((float) size::paneHeaderPadR);
        paint::text (g, label, text, type::paneHeader, colour::textMuted);

        if (rightText.isNotEmpty())
            paint::text (g, rightText, text, type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);
    }
}

void paintSubheader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, const juce::String& rightText)
{
    const auto r = area.toFloat();
    g.setColour (paint::c (colour::paneHeaderLine));
    paint::hline (g, r.getX(), r.getY(), r.getWidth());

    const auto text = r.withTrimmedLeft ((float) size::paneHeaderPadL).withTrimmedRight ((float) size::paneHeaderPadR);
    paint::text (g, label, text, type::paneHeader, colour::textMuted);

    if (rightText.isNotEmpty())
        paint::text (g, rightText, text, type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);
}

} // namespace encore::kit
