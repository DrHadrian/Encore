// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** An output's mute (NEW-FEATURES-M10-M11 §C): "M", 24 × 20, raised grey
    while the output plays, the MUTED gradient (#E8483E → #BF2C25, white)
    while it's muted. The stage variant is a big touch target reading
    "Mute" / "MUTED". A toggle: a click flips it and calls onClick; the
    owner sets the state from the engine (setToggleState, no notification). */
class MuteButton final : public juce::Button
{
public:
    enum class Style { letter, stage };

    explicit MuteButton (Style = Style::letter);

    /** The stage variant's text size, as the stage scales (1 = 15 px). */
    void setTextScale (float);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
    float textScale = 1.0f;
};

} // namespace encore::kit
