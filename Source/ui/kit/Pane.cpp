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

    if (shown != headerText || newRightText != rightText)
    {
        headerText = shown;
        rightText = newRightText;
        rightTextW = rightText.isEmpty() ? 0.0f : textWidth (font (type::paneHeaderNote), rightText) + 1.0f;
        repaint (0, 0, getWidth(), headerHeight);
    }
}

void Pane::setHeaderColours (juce::uint32 top, juce::uint32 bottom)
{
    headerTop = top;
    headerBot = bottom;
    updateHeaderShape();
    repaint (0, 0, getWidth(), headerHeight + 2);
}

void Pane::setHeaderDot (std::optional<juce::uint32> newDot)
{
    if (newDot != headerDot)
    {
        headerDot = newDot;
        repaint (0, 0, getWidth(), headerHeight + 2);
    }
}

void Pane::setHeaderRightInset (int px)
{
    if (px != rightInset)
    {
        rightInset = px;
        repaint (0, 0, getWidth(), headerHeight + 2);
    }
}

void Pane::setHeaderRightText (const juce::String& newRightText)
{
    if (newRightText != rightText)
    {
        rightText = newRightText;
        rightTextW = rightText.isEmpty() ? 0.0f : textWidth (font (type::paneHeaderNote), rightText) + 1.0f;
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
    headerFill = juce::ColourGradient::vertical (paint::c (headerTop), header.getY(), paint::c (headerBot), header.getBottom());
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

        auto text = getHeaderBounds().toFloat().withTrimmedLeft ((float) size::paneHeaderPadL)
                                               .withTrimmedRight ((float) (size::paneHeaderPadR + rightInset));

        if (headerDot.has_value())
        {
            // A 7 px status dot before the label (the block stage: on / off).
            const auto d = text.removeFromLeft ((float) size::headerDot).withSizeKeepingCentre ((float) size::headerDot, (float) size::headerDot);
            g.setColour (paint::c (*headerDot).withMultipliedAlpha (0.35f));
            g.fillEllipse (d.expanded (2.0f));
            g.setColour (paint::c (*headerDot));
            g.fillEllipse (d);
            text.removeFromLeft ((float) size::rowGap);
        }

        if (rightText.isNotEmpty())
        {
            paint::text (g, rightText, text.removeFromRight (juce::jmin (rightTextW, text.getWidth() * 0.6f)),
                         type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);
            text.removeFromRight ((float) size::rowGap);
        }

        paint::text (g, headerText, text, type::paneHeader, colour::textMuted);
    }
}

void paintSubheader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, const juce::String& rightText,
                     bool lineAbove)
{
    auto r = area.toFloat();

    if (lineAbove)
    {
        g.setColour (paint::c (colour::paneHeaderLine));
        paint::hline (g, r.getX(), r.getY(), r.getWidth());
        r.removeFromTop (1.0f);
    }

    const auto text = r.withTrimmedLeft ((float) size::paneHeaderPadL).withTrimmedRight ((float) size::paneHeaderPadR);
    paint::text (g, label, text, type::paneHeader, colour::textMuted);

    if (rightText.isNotEmpty())
        paint::text (g, rightText, text, type::paneHeaderNote, colour::textMuted, juce::Justification::centredRight);
}

} // namespace encore::kit
