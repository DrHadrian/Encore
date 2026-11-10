// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore
{

/** A horizontal level meter, readable from a distance.

    It doesn't poll anything itself: the owner calls setPeak() from its UI timer
    with the value taken from a PeakMeter. The bar falls back smoothly, the thin
    marker holds the recent peak, and the right end turns red near 0 dBFS.
*/
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText);

    /** UI thread: feed the newest block peak (linear, 0..1+). */
    void setPeak (float linearPeak);

    void paint (juce::Graphics&) override;

private:
    static float toProportion (float decibels) noexcept;

    juce::String label;
    float displayDb = -100.0f;
    float holdDb = -100.0f;
    int holdFrames = 0;
    int clipFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace encore
