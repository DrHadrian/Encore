// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/Icons.h"

namespace encore::kit
{

/** A 20 px icon-only button: transparent, the icon in secondary grey, a
    hover fill. Always has a title and a tooltip saying what it does. */
class IconButton final : public juce::Button
{
public:
    IconButton (theme::Icon icon, const juce::String& whatItDoes);

    void setIcon (theme::Icon);
    void setIconSize (float px) { iconSize = px; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    theme::Icon icon;
    float iconSize;
};

} // namespace encore::kit
