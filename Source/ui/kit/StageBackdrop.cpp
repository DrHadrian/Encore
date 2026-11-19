// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/StageBackdrop.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

void StageBackdrop::paint (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    const auto s = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto w = juce::roundToInt ((float) area.getWidth() * s), h = juce::roundToInt ((float) area.getHeight() * s);

    if (image.isNull() || image.getWidth() != w || image.getHeight() != h || ! juce::approximatelyEqual (s, scale))
    {
        scale = s;
        image = juce::Image (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
        juce::Graphics ig (image);
        const auto r = image.getBounds().toFloat();
        const juce::Point<float> centre (r.getWidth() * 0.45f, r.getHeight() * 0.25f);

        // An ellipse through the farthest corner (CSS farthest-corner).
        const auto rx = juce::jmax (centre.x, r.getWidth() - centre.x) * juce::MathConstants<float>::sqrt2;
        const auto ry = juce::jmax (centre.y, r.getHeight() - centre.y) * juce::MathConstants<float>::sqrt2;
        juce::ColourGradient light (paint::c (colour::blockStageTop), 0.0f, 0.0f, paint::c (colour::blockStageBot), 1.0f, 0.0f, true);
        light.addColour (0.65, paint::c (colour::blockStageMid));
        juce::FillType fill (light);
        fill.transform = juce::AffineTransform::scale (rx, ry).translated (centre);
        ig.setFillType (fill);
        ig.fillRect (r);
    }

    if (area != clipFor)
    {
        clipFor = area;
        clip.clear();
        clip.addRoundedRectangle ((float) area.getX(), (float) area.getY(), (float) area.getWidth(), (float) area.getHeight(),
                                  (float) size::paneRadius - 1.0f, (float) size::paneRadius - 1.0f, false, false, true, true);
    }

    // Inside the pane's rounded bottom corners.
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);
    g.drawImage (image, area.toFloat());
}

} // namespace encore::kit
