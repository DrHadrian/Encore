// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** An EQ's response (NEW-FEATURES-M10-M11 §C): log frequency 20 Hz – 20 kHz
    across, ±12 dB up and down, the curve in accent blue over a faint fill,
    on a dark well. Full size has a grid and the 20 Hz / 20 kHz / +12 dB
    labels; the mini one (an output's row) is just the curve. The owner gives
    the response (dB) at frequencies spread evenly in log; the path is built
    when that or the size changes, never in paint(). A click calls onClick
    (the Routing view resets the EQ). */
class EqCurve final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit EqCurve (bool full = true);

    /** dB at frequencies from 20 Hz to 20 kHz, evenly spaced in log. */
    void setResponse (std::vector<float> decibels);

    /** The frequency of point i of n (for the owner to compute the response). */
    static double frequencyAt (int index, int numPoints) noexcept;

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void rebuild();

    bool full;
    std::vector<float> response;
    juce::Path curve, fill, grid, zero;
};

} // namespace encore::kit
