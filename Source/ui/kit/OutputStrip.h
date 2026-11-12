// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "ui/kit/LevelBars.h"
#include "ui/kit/MuteButton.h"

namespace encore::kit
{

/** One output in the bottom row (NEW-FEATURES-M10-M11 §B.4): a box with the
    output's name and channels ("1/2"), its level bars (two for a stereo
    output), the M button and its level ("−6.0 dB", or "muted" in red).
    Compact strips share the row's width; full ones are 170 wide.
    onMuteClicked gets the new state; the owner sets it on the engine. */
class OutputStrip final : public juce::Component
{
public:
    OutputStrip();

    /** What it shows; repaints only when something changed. */
    void setOutput (const juce::String& name, const juce::String& channels, bool stereo, const juce::String& levelText, bool muted);

    /** UI timer: the output's peaks (linear). */
    void setPeaks (float left, float right) { bars.setPeaks (left, right); }

    std::function<void (bool muted)> onMuteClicked;

    const juce::String& getName() const noexcept { return name; }
    MuteButton& getMuteButton() noexcept { return mute; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name, channels, value;
    bool muted = false;
    LevelBars bars;
    MuteButton mute;
};

} // namespace encore::kit
