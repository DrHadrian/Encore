// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::theme
{

/** The UI icons (Resources/ui/icons, 24 x 24 stroke drawings) and the few
    glyphs Inter doesn't have (▸ ◂), as filled paths built once and drawn at
    any size in any colour. Message thread only. */
enum class Icon
{
    alert, arrowRight, audioWave, check, chevronDown, chevronUp, close, duplicate, jack, lock,
    merge, midiPort, minus, plug, plus, route, sidebarRight, sliders, split, trash, warn, window,
    triangleRight, triangleLeft
};

/** The icon's outline in a 24 x 24 box. */
const juce::Path& iconPath (Icon);

/** Draws an icon centred in `bounds` (square, fitted). */
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> bounds, juce::Colour);

/** The wordmark (Resources/ui/logo/encore-wordmark-ui.svg), fitted into
    `bounds` keeping its proportions. Never typed, never recoloured. */
void drawLogo (juce::Graphics&, juce::Rectangle<float> bounds);

/** The logo's width : height. */
float logoAspect();

} // namespace encore::theme
