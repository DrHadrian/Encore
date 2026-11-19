// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A top bar (DESIGN-SYSTEM.md §5.1): gradient, top highlight and a 1 px
    line under it. `sub` draws the darker sub-toolbar of the full list. The
    owner adds and places the children. The logo is drawn with drawLogo(). */
class Toolbar final : public juce::Component
{
public:
    explicit Toolbar (bool sub = false);

    /** Where the wordmark goes (empty: none). */
    void setLogoBounds (juce::Rectangle<int>);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const bool isSub;
    juce::Rectangle<int> logo;
    juce::ColourGradient fill;
};

} // namespace encore::kit
