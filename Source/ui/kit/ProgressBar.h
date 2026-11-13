// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A thin progress bar (NEW-FEATURES-M10-M11 §C): 4 px recessed track, the
    accent fill. No animation: it moves only when setProgress changes it. */
class ProgressBar final : public juce::Component
{
public:
    ProgressBar();

    /** 0..1 (clamped). */
    void setProgress (double proportion);
    double getProgress() const noexcept { return progress; }

    void paint (juce::Graphics&) override;

private:
    double progress = 0.0;
};

} // namespace encore::kit
