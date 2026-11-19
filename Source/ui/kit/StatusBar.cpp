// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/StatusBar.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    const type::Style& valueStyle (bool stage) noexcept { return stage ? type::statusValueStage : type::statusValue; }
    const type::Style& labelStyle (bool stage) noexcept { return stage ? type::statusLabelStage : type::statusLabel; }
    constexpr float border = 1.0f, line = 1.0f;
    constexpr float padX = (float) size::statusBarPadX;
}

int StatusBar::heightFor (bool stage) noexcept
{
    return (stage ? size::lcdStage : size::lcdEdit) + 2;   // inside a 1 px border
}

StatusBar::StatusBar (bool stage) : isStage (stage)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);   // rounded corners over the toolbar
    layOut();
}

std::vector<StatusBar::Item> StatusBar::getItems() const
{
    std::vector<Item> items;

    for (const auto& s : shown)
        items.push_back (s.item);

    return items;
}

void StatusBar::setItems (std::vector<Item> newItems)
{
    // Only what changed is repainted; a change of width re-lays the row.
    bool sameShape = newItems.size() == shown.size();

    for (size_t i = 0; sameShape && i < newItems.size(); ++i)
        sameShape = newItems[i].label == shown[i].item.label && newItems[i].minWidth == shown[i].item.minWidth
                    && newItems[i].maxWidth == shown[i].item.maxWidth;

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

void StatusBar::setAvailableWidth (int width)
{
    if (width != availableWidth)
    {
        availableWidth = width;
        layOut();
        repaint();
    }
}

void StatusBar::layOut()
{
    const auto valueFont = font (valueStyle (isStage));
    const auto labelFont = font (labelStyle (isStage));

    const auto place = [this]
    {
        // [border][pad text pad][line][pad text pad]...[border]
        auto x = border;

        for (size_t i = 0; i < shown.size(); ++i)
        {
            if (i > 0)
                x += line;

            shown[i].x = x + padX;
            x += padX + shown[i].width + padX;
        }

        idealWidth = (int) std::ceil (x + border);
    };

    for (auto& s : shown)
    {
        s.caps = s.item.label.toUpperCase();
        const auto w = juce::jmax (textWidth (valueFont, s.item.value), textWidth (labelFont, s.caps));
        s.width = (float) juce::jmax (s.item.minWidth, (int) std::ceil (w));

        if (s.item.maxWidth > 0)
            s.width = juce::jmin (s.width, (float) juce::jmax (s.item.minWidth, s.item.maxWidth));
    }

    place();

    // Too wide for the room: the cuttable fields give way.
    if (availableWidth > 0 && idealWidth > availableWidth)
    {
        auto excess = (float) (idealWidth - availableWidth);

        for (auto& s : shown)
        {
            if (s.item.maxWidth > 0 && excess > 0.0f)
            {
                const auto give = juce::jmax (0.0f, juce::jmin (excess, s.width - (float) juce::jmin (s.item.minWidth, size::lcdCutMinW)));
                s.width -= give;
                excess -= give;
            }
        }

        place();
    }
}

juce::Rectangle<float> StatusBar::cellBounds (const Shown& s) const
{
    const auto valueHeight = valueStyle (isStage).px * type::lcdLineHeight;
    const auto labelHeight = labelStyle (isStage).px * type::lcdLineHeight;
    const auto top = ((float) getHeight() - valueHeight - labelHeight) * 0.5f;
    const auto right = (float) getWidth() - border - padX;
    return { s.x, top, juce::jmax (0.0f, juce::jmin (s.width, right - s.x)), valueHeight + labelHeight };
}

void StatusBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = (float) size::statusBarRadius;

    // Flat: no gradient, no inner shadow (V3.2-CHANGES §5).
    g.setColour (paint::c (colour::statusBarBg));
    g.fillRoundedRectangle (r, radius);
    g.setColour (paint::c (colour::statusBarEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    const auto labelHeight = labelStyle (isStage).px * type::lcdLineHeight;

    for (size_t i = 0; i < shown.size(); ++i)
    {
        const auto& s = shown[i];

        // The line before every field but the first, full height inside the border.
        if (i > 0)
        {
            g.setColour (paint::c (colour::statusBarEdge));
            g.fillRect (juce::Rectangle<float> (s.x - padX - line, border, line, r.getHeight() - 2.0f * border));
        }

        const auto cell = cellBounds (s);

        if (cell.getWidth() < 4.0f)
            break;

        paint::text (g, s.caps, cell.withHeight (labelHeight), labelStyle (isStage), colour::statusBarLabel);
        paint::text (g, s.item.value, cell.withTrimmedTop (labelHeight), valueStyle (isStage), colour::statusBarText);
    }
}

} // namespace encore::kit
