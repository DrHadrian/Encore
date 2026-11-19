// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "ui/kit/LevelBars.h"
#include "ui/kit/MuteButton.h"

namespace encore::kit
{

/** One output in the collapsed bottom row (V3.2-CHANGES §3.2): no box, the
    row's strips share its width with a 1 px divider between them (the
    owner draws it). Line 1: the name (11.5 semibold, its full name in the
    tooltip) and the channels ("1/2", right). Line 2: the level bars (two for
    a stereo output), the level ("0.0 dB", always shown; dim while muted)
    and M. Narrower than 140 px, the level goes on its own line under the
    bars. onMuteClicked gets the new state; the owner sets it on the engine. */
class OutputStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    OutputStrip();

    /** What it shows; repaints only when something changed. */
    void setOutput (const juce::String& name, const juce::String& channels, bool stereo, const juce::String& levelText, bool muted);

    /** Horizontal padding inside the strip's share (12 / 0 at the row's ends). */
    void setPadding (int left, int right);

    /** UI timer: the output's peaks (linear). */
    void setPeaks (float left, float right) { bars.setPeaks (left, right); }

    std::function<void (bool muted)> onMuteClicked;

    const juce::String& getName() const noexcept { return name; }
    const juce::String& getValueText() const noexcept { return value; }
    bool isValueOnOwnLine() const noexcept { return valueOwnLine; }
    MuteButton& getMuteButton() noexcept { return mute; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name, channels, value;
    bool muted = false, valueOwnLine = false;
    int padLeft = 0, padRight = 0;
    float channelsWidth = 0.0f;
    juce::Rectangle<float> nameArea, channelsArea, valueArea;
    LevelBars bars;
    MuteButton mute;
};

} // namespace encore::kit
