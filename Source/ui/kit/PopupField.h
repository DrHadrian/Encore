// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A raised field that opens a menu (V3.2-CHANGES §3.2, the input picker):
    22 px (compact: 20), radius 5, #46464B → #38383C, border #0C0C0E; the
    name in semibold, then the caption in #9A9AA1 (ellipsised), a 9 px
    chevron at the right. A name that isn't on this computer shows in amber.
    The owner shows the menu in onClick. Clicks never take focus. */
class PopupField final : public juce::Button
{
public:
    PopupField();

    void setText (const juce::String& name, const juce::String& caption, bool missing = false);
    void setCompact (bool);   // the mixer view's: 20 px, 11 px, the name only

    const juce::String& getName() const noexcept { return name; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String name, caption, separated;
    bool missing = false, compact = false;
    float nameWidth = 0.0f;
};

} // namespace encore::kit
