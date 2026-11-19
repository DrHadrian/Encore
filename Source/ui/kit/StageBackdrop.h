// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The block stage's lit backdrop (radial gradient at 45 % 25 %, #3A3A3E →
    #1C1C1F at 65 % → #141416), inside a pane's rounded bottom corners.
    Rendered when the size or the screen scale changes; paint() only copies
    it. For the block stage and the IN / OUT stages. */
class StageBackdrop
{
public:
    void paint (juce::Graphics&, juce::Rectangle<int> area);

private:
    juce::Image image;
    juce::Path clip;
    juce::Rectangle<int> clipFor;
    float scale = 0.0f;
};

} // namespace encore::kit
