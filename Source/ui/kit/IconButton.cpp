// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/IconButton.h"

#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

IconButton::IconButton (Icon i, const juce::String& whatItDoes)
    : juce::Button (whatItDoes), icon (i), iconSize ((float) size::iconSize)
{
    setTitle (whatItDoes);
    setTooltip (whatItDoes);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void IconButton::setIcon (Icon i)
{
    if (i != icon)
    {
        icon = i;
        repaint();
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    const auto enabled = isEnabled();
    const auto lit = enabled && (highlighted || down || getToggleState());

    if (lit)
    {
        g.setColour (paint::c (colour::iconButtonHover));
        g.fillRoundedRectangle (r, 4.0f);
    }

    auto colourToUse = paint::c (lit ? colour::textStrong : colour::textSecondary);

    if (! enabled)
        colourToUse = colourToUse.withMultipliedAlpha (state::disabledAlpha);

    drawIcon (g, icon, r.withSizeKeepingCentre (iconSize, iconSize), colourToUse);
}

} // namespace encore::kit
