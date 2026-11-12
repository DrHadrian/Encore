// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The send law (NEW-FEATURES-M10-M11 §B.3): where a level sits on a fader,
    0 at +6 dB and 1 at −∞ (bottom / left end), piecewise linear between
    +6 → 0, 0 → 0.1, −6 → 0.25, −12 → 0.4, −24 → 0.6, −40 → 0.8, −∞ → 1. */
namespace faderLaw
{
    constexpr float silentDb = -100.0f;   // at or below: −∞ (off)

    float positionFor (float dB) noexcept;   // 0 (top) .. 1 (bottom)
    float dbFor (float position) noexcept;
}

/** A level fader for sends and outputs (NEW-FEATURES-M10-M11 §C):
    - vertical (a send strip): a 196 px track with the scale beside it
      (+6, 0, −6, −12, −24, −40, −∞), the fill from the thumb down, a
      28 × 14 cap with a centre line;
    - horizontal (an output's row): a 4 px track filled to a 12 px round
      thumb, as the volume slider.
    Drag to change, double-click for 0 dB, the wheel nudges. onChange gets
    the new level while it moves; onGestureEnd when the mouse lets go. */
class Fader final : public juce::Component
{
public:
    enum class Orientation { vertical, horizontal };

    explicit Fader (Orientation);

    void setDb (float dB);          // no callback
    float getDb() const noexcept { return db; }

    /** Dimmed (an output not here, a send switched off): still works. */
    void setDimmed (bool);

    std::function<void (float dB)> onChange;
    std::function<void()> onGestureEnd;

    /** The track (in this component), for a meter to line up with. */
    juce::Rectangle<float> getTrackBounds() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void setFromUser (float newDb);
    float positionOf (juce::Point<float>) const;

    Orientation orientation;
    float db = 0.0f, dragStartPosition = 0.0f;
    bool dimmed = false;
};

} // namespace encore::kit
