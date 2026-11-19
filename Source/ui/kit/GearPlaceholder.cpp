// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/GearPlaceholder.h"

#include <map>
#include <tuple>

#include "ui/kit/KnobView.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

juce::Rectangle<int> GearView::idealBounds (Size s, BlockInfo::Shape shape)
{
    using Shape = BlockInfo::Shape;

    switch (s)
    {
        case Size::chainTile:  return { shape == Shape::amp ? size::ampTileW : size::tileW, size::tileH };
        case Size::stagePedal: return { size::pedalW, size::pedalH };
        case Size::panel:
            if (shape == Shape::amp) return { size::ampPanelW, size::ampPanelH };
            if (shape == Shape::cab) return { size::cabPanelW, size::cabPanelH };
            return { size::ampPanelW, size::blockPanelH };
    }

    return {};
}

GearPlaceholder::GearPlaceholder (Size s, const BlockInfo& info) : GearView (s), block (info)
{
    setInterceptsMouseClicks (false, false);   // the owner takes the clicks, as before
    update();
}

void GearPlaceholder::setBlock (const BlockInfo& info)
{
    if (info != block)
    {
        block = info;
        update();
        repaint();
    }
}

void GearPlaceholder::setOn (bool on)
{
    if (on != block.on)
    {
        block.on = on;
        repaint (ledArea.expanded ((float) size::ledGlowR).getSmallestIntegerContainer());
    }
}

juce::Rectangle<int> GearPlaceholder::getControlArea() const
{
    if (getGearSize() != Size::panel || block.shape == BlockInfo::Shape::cab)
        return {};

    // 22 px in from both sides (556 on the 600 box).
    return { size::ampStripX, size::ampStripY, getWidth() - 2 * size::ampStripX, size::ampStripH };
}

void GearPlaceholder::setLabelBottom (int fromBottom)
{
    if (fromBottom == labelBottom)
        return;

    labelBottom = fromBottom;
    update();
    repaint();
}

bool GearPlaceholder::isPedal() const noexcept
{
    // Amps and cabs are boxes at every size; everything else is a pedal
    // except on the Edit panel, where every block gets a box.
    return block.shape == BlockInfo::Shape::pedal && getGearSize() != Size::panel;
}

void GearPlaceholder::resized()
{
    update();
}

void GearPlaceholder::update()
{
    const auto bounds = getLocalBounds().toFloat();
    kindCaps = block.kindLabel.toUpperCase();
    plateCaps = block.plate.toUpperCase();
    boxShape.clear();
    labelArea = plateArea = stripArea = ledArea = nameArea = footswitchArea = {};

    if (bounds.isEmpty())
        return;

    if (isPedal())
    {
        // Laid out at 160 × 210, scaled into the component (aspect kept, centred).
        pedalScale = juce::jmin (bounds.getWidth() / (float) size::pedalW, bounds.getHeight() / (float) size::pedalH);
        const auto box = juce::Rectangle<float> ((float) size::pedalW * pedalScale, (float) size::pedalH * pedalScale)
                             .withCentre (bounds.getCentre());
        const auto at = [&] (float x, float y, float w, float h)
        {
            return juce::Rectangle<float> (box.getX() + x * pedalScale, box.getY() + y * pedalScale, w * pedalScale, h * pedalScale);
        };

        const auto radius = (float) size::pedalRadius * pedalScale;
        boxShape.addRoundedRectangle (box, radius);
        boxFill = juce::ColourGradient::vertical (paint::c (colour::pedalTop), box.getY(), paint::c (colour::pedalBot), box.getBottom());

        const auto px = (float) size::pedalH / (float) size::gearLabelDivisor;   // 15 at full size
        kindStyle = { px * pedalScale, type::gearKind.weight, type::gearKind.tracking, true };
        labelArea = at ((float) size::pedalInset, (float) size::pedalInset,
                        (float) (size::pedalW - 2 * size::pedalInset), px * 1.2f);
        labelJustification = juce::Justification::centredLeft;

        ledArea = at ((float) (size::pedalW - size::pedalLed) * 0.5f, (float) size::pedalLedY, (float) size::pedalLed, (float) size::pedalLed);
        nameArea = at (0.0f, (float) size::pedalNameY, (float) size::pedalW, (float) size::pedalNameH);
        footswitchArea = at ((float) (size::pedalW - size::footswitch) * 0.5f, (float) size::footswitchY,
                             (float) size::footswitch, (float) size::footswitch);
        footswitchFill = juce::ColourGradient::vertical (paint::c (colour::footswitchTop), footswitchArea.getY(),
                                                         paint::c (colour::footswitchBot), footswitchArea.getBottom());
        return;
    }

    // Amp, cab and block boxes: radius 10 at every size.
    pedalScale = 1.0f;
    boxShape.addRoundedRectangle (bounds, (float) size::gearRadius);
    boxFill = juce::ColourGradient::vertical (paint::c (colour::gearTop), 0.0f, paint::c (colour::gearBot), bounds.getBottom());

    const auto panelWithStrip = getGearSize() == Size::panel && block.shape != BlockInfo::Shape::cab;

    if (panelWithStrip)
    {
        // Amp (and other blocks) on the Edit panel: strip, plate, kind label at the bottom.
        stripArea = getControlArea().toFloat();
        kindStyle = type::gearPanelKind;
        labelArea = { 0.0f, bounds.getBottom() - (float) labelBottom, bounds.getWidth(), (float) size::panelLabelH };
        labelJustification = juce::Justification::centred;

        if (plateCaps.isNotEmpty())
        {
            const auto w = textWidth (font (type::plate), plateCaps) + (float) size::platePadX * 2.0f;
            plateArea = { bounds.getRight() - (float) size::plateRight - w, bounds.getBottom() - (float) size::plateBottom - (float) size::plateH,
                          w, (float) size::plateH };
        }
    }
    else
    {
        // Chain tiles and the cab panel: the kind label in the middle, height / 14.
        const auto px = juce::jmax ((float) size::gearLabelMin, bounds.getHeight() / (float) size::gearLabelDivisor);
        kindStyle = { px, type::gearKind.weight, type::gearBoxTrackingPx / px, true };
        labelArea = bounds;
        labelJustification = juce::Justification::centred;
    }
}

void GearPlaceholder::paint (juce::Graphics& g)
{
    if (boxShape.isEmpty())
        return;

    const auto pedal = isPedal();
    const auto box = boxShape.getBounds();

    g.setGradientFill (boxFill);
    g.fillPath (boxShape);

    // Inner top highlight (white 8 %) and the border (pedals 2 px at full size).
    const auto radius = pedal ? (float) size::pedalRadius * pedalScale : (float) size::gearRadius;
    const auto border = pedal ? (float) size::pedalBorder * pedalScale : 1.0f;
    g.setColour (paint::c (colour::gearHighlight));
    paint::hline (g, box.getX() + radius, box.getY() + border, box.getWidth() - radius * 2.0f);
    g.setColour (paint::c (colour::gearBorder));
    g.drawRoundedRectangle (box.reduced (border * 0.5f), juce::jmax (0.0f, radius - border * 0.5f), border);

    if (! stripArea.isEmpty())
    {
        g.setColour (paint::c (colour::gearStrip));
        g.fillRoundedRectangle (stripArea, (float) size::stripRadius);
        g.setColour (paint::c (colour::gearStripBorder));
        g.drawRoundedRectangle (stripArea.reduced (0.5f), (float) size::stripRadius - 0.5f, 1.0f);
    }

    if (! plateArea.isEmpty())
    {
        g.setColour (paint::c (colour::modelPlate));
        g.fillRoundedRectangle (plateArea, (float) size::plateRadius);
        paint::text (g, plateCaps, plateArea, type::plate, colour::pedalName, juce::Justification::centred);
    }

    paint::text (g, kindCaps, labelArea, kindStyle, colour::gearLabel, labelJustification);

    if (! pedal)
        return;

    paint::text (g, block.name, nameArea, type::pedalName, colour::pedalName, juce::Justification::centred, pedalScale);
    paintLed (g, ledArea, block.on, ledArea.getWidth() / (float) size::pedalLed);

    g.setGradientFill (footswitchFill);
    g.fillEllipse (footswitchArea);
    const auto ring = (float) size::footswitchBorderW * pedalScale;
    g.setColour (paint::c (colour::footswitchBorder));
    g.drawEllipse (footswitchArea.reduced (ring * 0.5f), ring);
}

void GearPlaceholder::paintPedalShadow (juce::Graphics& g, juce::Rectangle<float> pedalBounds)
{
    // One image per (size, screen scale), kept while the app runs; a handful
    // of sizes exist (chain tiles, stage pedals at a few widget sizes).
    JUCE_ASSERT_MESSAGE_THREAD

    if (pedalBounds.isEmpty())
        return;

    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto s = juce::jmin (pedalBounds.getWidth() / (float) size::pedalW, pedalBounds.getHeight() / (float) size::pedalH);
    const auto box = juce::Rectangle<float> ((float) size::pedalW * s, (float) size::pedalH * s).withCentre (pedalBounds.getCentre());
    const auto blur = (float) size::pedalShadowBlur * s, dy = (float) size::pedalShadowY * s;
    const auto margin = blur + dy + 1.0f;

    using Key = std::tuple<int, int, int>;
    static std::map<Key, juce::Image> cache;
    const Key key { juce::roundToInt (box.getWidth() * 100.0f), juce::roundToInt (box.getHeight() * 100.0f), juce::roundToInt (scale * 100.0f) };
    auto found = cache.find (key);

    if (found == cache.end())
    {
        if (cache.size() > 32)
            cache.clear();

        const auto area = juce::Rectangle<float> (box.getWidth() + margin * 2.0f, box.getHeight() + margin * 2.0f);
        juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (area.getWidth() * scale)),
                           juce::jmax (1, juce::roundToInt (area.getHeight() * scale)), true);
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            juce::Path shape;
            shape.addRoundedRectangle (juce::Rectangle<float> (margin, margin, box.getWidth(), box.getHeight()), (float) size::pedalRadius * s);
            juce::DropShadow (paint::c (colour::pedalShadow), juce::jmax (1, juce::roundToInt (blur)), { 0, juce::roundToInt (dy) })
                .drawForPath (ig, shape);
        }

        found = cache.emplace (key, image).first;
    }

    g.drawImage (found->second, box.expanded (margin));
}

} // namespace encore::kit
