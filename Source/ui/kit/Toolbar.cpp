// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Toolbar.h"

#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

Toolbar::Toolbar (bool sub) : isSub (sub)
{
    setOpaque (true);
}

void Toolbar::setLogoBounds (juce::Rectangle<int> bounds)
{
    if (bounds != logo)
    {
        repaint (logo);
        logo = bounds;
        repaint (logo);
    }
}

void Toolbar::resized()
{
    // The gradient runs under the bottom line too, as on the boards.
    fill = juce::ColourGradient::vertical (paint::c (isSub ? colour::subToolbarTop : colour::toolbarTop), 0.0f,
                                           paint::c (isSub ? colour::subToolbarBot : colour::toolbarBot), (float) getHeight());
}

void Toolbar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (fill);
    g.fillRect (r);

    if (! isSub)
    {
        g.setColour (paint::c (colour::toolbarHighlight));
        paint::hline (g, 0.0f, 0.0f, r.getWidth());
    }

    g.setColour (paint::c (isSub ? colour::paneHeaderLine : colour::paneBorder));
    paint::hline (g, 0.0f, r.getBottom() - 1.0f, r.getWidth());

    if (! logo.isEmpty())
        drawLogo (g, logo.toFloat());
}

} // namespace encore::kit
