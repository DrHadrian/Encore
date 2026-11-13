// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/MuteButton.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

MuteButton::MuteButton (Style s) : juce::Button ("Mute"), style (s)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTooltip ("Mute this output (only this one)");
}

void MuteButton::setTextScale (float newScale)
{
    if (juce::approximatelyEqual (newScale, textScale))
        return;

    textScale = newScale;
    repaint();
}

void MuteButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto r = getLocalBounds().toFloat();
    const auto radius = style == Style::letter ? (float) size::outputMuteRadius : (float) size::stageMuteRadius;
    const auto lighten = down ? 0.0f : (highlighted ? state::hoverLighten : 0.0f);

    if (down)
        paint::pressed (g, r, radius);
    else if (on)
        paint::raised (g, r, colour::mutedTop, colour::mutedBot, colour::mutedBorder, radius, true, lighten);
    else
        paint::raised (g, r, colour::buttonTop, colour::buttonBot, colour::buttonBorder, radius, true, lighten);

    const auto ink = on ? colour::textStrong : (style == Style::letter ? colour::muteOffText : colour::text);

    if (style == Style::letter)
        paint::text (g, "M", r, type::muteLetter, ink, juce::Justification::centred);
    else
        paint::text (g, on ? "MUTED" : "Mute", r, type::stageMute, ink, juce::Justification::centred, textScale);
}

} // namespace encore::kit
