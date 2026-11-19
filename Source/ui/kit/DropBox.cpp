// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/DropBox.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

DropBox::DropBox()
{
    setInterceptsMouseClicks (false, false);
}

void DropBox::setLabel (const juce::String& text)
{
    if (text != label)
    {
        label = text;
        repaint();
    }
}

void DropBox::setRefused (bool r)
{
    if (r != refused)
    {
        refused = r;
        repaint();
    }
}

void DropBox::resized()
{
    const auto w = (float) size::dropBoxBorder;
    juce::Path outline;
    outline.addRoundedRectangle (getLocalBounds().toFloat().reduced (w * 0.5f), (float) size::dropBoxRadius - w * 0.5f);
    const float pattern[] { (float) size::dropBoxDash, (float) size::dropBoxDashGap };
    juce::PathStrokeType (w).createDashedStroke (dashes, outline, pattern, 2);
}

void DropBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (paint::c (refused ? colour::dropBoxRefused : colour::dropBoxFill));
    g.fillRoundedRectangle (r, (float) size::dropBoxRadius);
    g.setColour (paint::c (refused ? colour::amber : colour::accent));
    g.fillPath (dashes);
    paint::text (g, label, r.reduced ((float) size::dropBoxRadius, 0.0f), type::dropBox, colour::dropBoxText, juce::Justification::centred);
}

} // namespace encore::kit
