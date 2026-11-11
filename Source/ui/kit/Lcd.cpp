// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Lcd.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    const type::Style& valueStyle (bool stage) noexcept { return stage ? type::lcdValueStage : type::lcdValue; }
    const type::Style& labelStyle (bool stage) noexcept { return stage ? type::lcdLabelStage : type::lcdLabel; }
    float padX (bool stage) noexcept    { return (float) (stage ? size::lcdPadXStage : size::lcdPadX) + 1.0f; }   // + border
    float itemGap (bool stage) noexcept { return (float) (stage ? size::lcdGapStage : size::lcdGap); }
}

int Lcd::heightFor (bool stage) noexcept
{
    return (stage ? size::lcdStage : size::lcdEdit) + 3;   // 1 px border top and bottom, 1 px highlight under
}

Lcd::Lcd (bool stage) : isStage (stage)
{
    setInterceptsMouseClicks (false, false);
    layOut();
}

void Lcd::setItems (std::vector<Item> newItems)
{
    // Only what changed is repainted; a change of width re-lays the row.
    bool sameShape = newItems.size() == shown.size();

    for (size_t i = 0; sameShape && i < newItems.size(); ++i)
        sameShape = newItems[i].label == shown[i].item.label && newItems[i].minWidth == shown[i].item.minWidth;

    if (sameShape)
    {
        std::vector<size_t> changed;

        for (size_t i = 0; i < newItems.size(); ++i)
            if (newItems[i] != shown[i].item)
                changed.push_back (i);

        if (changed.empty())
            return;

        const auto oldWidth = idealWidth;
        std::vector<float> oldWidths;

        for (const auto& s : shown)
            oldWidths.push_back (s.width);

        for (size_t i = 0; i < newItems.size(); ++i)
            shown[i].item = std::move (newItems[i]);

        layOut();

        bool widthsKept = idealWidth == oldWidth;

        for (size_t i = 0; widthsKept && i < shown.size(); ++i)
            widthsKept = juce::approximatelyEqual (shown[i].width, oldWidths[i]);

        if (widthsKept)
        {
            for (auto i : changed)
                repaint (cellBounds (shown[i]).getSmallestIntegerContainer());

            return;
        }

        repaint();
        return;
    }

    shown.clear();

    for (auto& item : newItems)
        shown.push_back ({ std::move (item), {}, 0.0f, 0.0f });

    layOut();
    repaint();
}

void Lcd::layOut()
{
    const auto valueFont = font (valueStyle (isStage));
    const auto labelFont = font (labelStyle (isStage));
    auto x = padX (isStage);

    for (size_t i = 0; i < shown.size(); ++i)
    {
        auto& s = shown[i];
        s.caps = s.item.label.toUpperCase();
        const auto w = juce::jmax (textWidth (valueFont, s.item.value), textWidth (labelFont, s.caps));
        s.width = (float) juce::jmax (s.item.minWidth, (int) std::ceil (w));
        s.x = x;
        x += s.width + itemGap (isStage);
    }

    idealWidth = (int) std::ceil (shown.empty() ? padX (isStage) * 2.0f : x - itemGap (isStage) + padX (isStage));
}

juce::Rectangle<float> Lcd::cellBounds (const Shown& s) const
{
    const auto valueHeight = valueStyle (isStage).px * type::lcdLineHeight;
    const auto labelHeight = labelStyle (isStage).px * type::lcdLineHeight;
    const auto top = ((float) getHeight() - 1.0f - valueHeight - labelHeight) * 0.5f;
    const auto right = (float) getWidth() - padX (isStage);
    return { s.x, top, juce::jmax (0.0f, juce::jmin (s.width, right - s.x)), valueHeight + labelHeight };
}

void Lcd::resized()
{
    const auto r = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    const auto radius = (float) size::lcdRadius;
    outline.clear();
    outline.addRoundedRectangle (r, radius);
    fill = juce::ColourGradient::vertical (paint::c (colour::lcdTop), 0.0f, paint::c (colour::lcdBot), r.getBottom());
    shadow = juce::ColourGradient::vertical (paint::c (colour::lcdShadow), 0.0f,
                                             paint::c (colour::lcdShadow).withAlpha (0.0f), (float) size::lcdShadowH);
}

void Lcd::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    const auto radius = (float) size::lcdRadius;

    g.setGradientFill (fill);
    g.fillPath (outline);

    {
        const juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (outline);
        g.setGradientFill (shadow);
        g.fillRect (r.withHeight ((float) size::lcdShadowH));
    }

    // The 1 px highlight under the box (box-shadow 0 1px 0 white 5 %).
    g.setColour (paint::c (colour::lcdHighlight));
    paint::hline (g, r.getX() + radius, r.getBottom(), r.getWidth() - radius * 2.0f);

    g.setColour (paint::c (colour::lcdBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    const auto valueHeight = valueStyle (isStage).px * type::lcdLineHeight;

    for (const auto& s : shown)
    {
        const auto cell = cellBounds (s);

        if (cell.getWidth() < 4.0f)
            break;

        paint::text (g, s.item.value, cell.withHeight (valueHeight), valueStyle (isStage), colour::lcdText);
        paint::text (g, s.caps, cell.withTrimmedTop (valueHeight), labelStyle (isStage), colour::lcdLabel);
    }
}

} // namespace encore::kit
