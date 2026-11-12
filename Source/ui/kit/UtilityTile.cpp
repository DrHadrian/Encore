// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/UtilityTile.h"

#include <map>

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

UtilityTile::UtilityTile (const UtilityTileInfo& i)
{
    setInterceptsMouseClicks (false, false);
    info = i;
    labelCaps = paint::caps (type::utilityLabel, info.label);
}

void UtilityTile::setInfo (const UtilityTileInfo& i)
{
    if (i == info)
        return;

    info = i;
    labelCaps = paint::caps (type::utilityLabel, info.label);
    update();
    repaint();
}

void UtilityTile::resized()
{
    update();
}

void UtilityTile::update()
{
    // One line when the name fits; else broken at the last space that lets
    // the first line fit; each line ellipsised by drawText.
    nameLines.clear();
    const auto width = (float) (getWidth() - 2 * size::utilityTilePadX);
    const auto f = font (type::utilityName);
    const auto name = info.name.trim();

    if (name.isEmpty())
        return;

    if (width <= 0.0f || textWidth (f, name) <= width || ! name.containsChar (' '))
    {
        nameLines.add (name);
        return;
    }

    auto split = name.lastIndexOfChar (' ');

    while (split > 0 && textWidth (f, name.substring (0, split)) > width)
        split = name.substring (0, split).lastIndexOfChar (' ');

    if (split <= 0)
        split = name.indexOfChar (' ');

    nameLines.add (name.substring (0, split).trim());
    nameLines.add (name.substring (split + 1).trim());
}

void UtilityTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = (float) size::utilityTileRadius;

    // Body: gradient, 1 px border (amber on a problem), inner top highlight.
    paint::raised (g, r, colour::utilityTileTop, colour::utilityTileBottom,
                   info.amber ? colour::amber : colour::utilityTileBorder, radius, false, 0.0f, colour::gearHighlight);

    const auto padX = (float) size::utilityTilePadX;
    const auto inner = r.reduced (padX, 0.0f);

    if (labelCaps.isNotEmpty())
        paint::text (g, labelCaps, inner.withY ((float) size::utilityLabelTop).withHeight ((float) size::utilityLabelH),
                     type::utilityLabel, colour::textMuted);

    if (info.dot != UtilityTileInfo::Dot::none && getWidth() >= size::utilityDotMinW)
    {
        const auto d = (float) size::utilityDot;
        const auto dot = juce::Rectangle<float> (r.getRight() - (float) size::utilityDotRight - d, (float) size::utilityDotTop, d, d);
        const auto c = info.dot == UtilityTileInfo::Dot::on    ? colour::healthy
                     : info.dot == UtilityTileInfo::Dot::amber ? colour::amber
                                                               : colour::dotOff;

        if (info.dot != UtilityTileInfo::Dot::off)
        {
            // The glow (0 0 6 px): a soft ring around it.
            g.setColour (paint::c (c).withMultipliedAlpha (0.35f));
            g.fillEllipse (dot.expanded (2.0f));
        }

        g.setColour (paint::c (c));
        g.fillEllipse (dot);
    }

    auto y = (float) size::utilityNameTop;

    for (auto& line : nameLines)
    {
        paint::text (g, line, inner.withY (y).withHeight ((float) size::utilityNameLineH + 1.0f), type::utilityName,
                     info.amber ? colour::faultCardText : colour::text);
        y += (float) size::utilityNameLineH;
    }

    if (info.caption.isNotEmpty())
        paint::text (g, info.caption, inner.withY ((float) size::utilityCaptionTop).withHeight ((float) size::utilityCaptionH),
                     type::utilityCaption, colour::textMuted);

    if (info.footswitch)
    {
        const auto foot = juce::Rectangle<float> ((float) size::utilityFootW, (float) size::utilityFootH)
                              .withCentre ({ r.getCentreX(), (float) size::utilityFootTop + (float) size::utilityFootH * 0.5f });
        paint::verticalGradient (g, foot, colour::footswitchTop, colour::footswitchBot, (float) size::utilityFootRadius);
        g.setColour (paint::c (colour::footswitchBorder));
        g.drawRoundedRectangle (foot.reduced (0.5f), (float) size::utilityFootRadius - 0.5f, 1.0f);
    }
}

void UtilityTile::paintShadow (juce::Graphics& g, juce::Rectangle<float> tileBounds)
{
    // One image per (size, screen scale), kept while the app runs.
    JUCE_ASSERT_MESSAGE_THREAD

    if (tileBounds.isEmpty())
        return;

    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto blur = (float) size::utilityShadowBlur, dy = (float) size::utilityShadowY;
    const auto margin = blur + dy + 1.0f;

    using Key = std::tuple<int, int, int>;
    static std::map<Key, juce::Image> cache;
    const Key key { juce::roundToInt (tileBounds.getWidth() * 100.0f), juce::roundToInt (tileBounds.getHeight() * 100.0f),
                    juce::roundToInt (scale * 100.0f) };
    auto found = cache.find (key);

    if (found == cache.end())
    {
        if (cache.size() > 32)
            cache.clear();

        const auto area = juce::Rectangle<float> (tileBounds.getWidth() + margin * 2.0f, tileBounds.getHeight() + margin * 2.0f);
        juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (area.getWidth() * scale)),
                           juce::jmax (1, juce::roundToInt (area.getHeight() * scale)), true);
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            juce::Path shape;
            shape.addRoundedRectangle (juce::Rectangle<float> (margin, margin, tileBounds.getWidth(), tileBounds.getHeight()),
                                       (float) size::utilityTileRadius);
            juce::DropShadow (paint::c (colour::utilityShadow), juce::jmax (1, juce::roundToInt (blur)), { 0, juce::roundToInt (dy) })
                .drawForPath (ig, shape);
        }

        found = cache.emplace (key, image).first;
    }

    g.drawImage (found->second, tileBounds.expanded (margin));
}

} // namespace encore::kit
