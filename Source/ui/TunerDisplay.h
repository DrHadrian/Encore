// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/Tuner.h"

namespace encore
{

/** The tuner (SCREENS.md §3.5): the note, big, on an LCD-styled panel, a
    scale of 21 ticks from -50 to +50 cents with the nearest one lit, and the
    cents, frequency and reference pitch. Green when in tune, amber close,
    red far off. Readable from across a stage at any size.

    Overlay (the default): covers its bounds with the dimmed tuner backdrop
    and shows the panel in a pane with "Done" under it (any click closes).
    Inside a stage widget: just the panel, filling the widget. */
class TunerDisplay final : public juce::Component
{
public:
    TunerDisplay();

    void setReading (const Tuner::Reading& reading, bool outputMuted, float referencePitch);

    /** false: drawn inside a stage widget (no backdrop, no pane, no Done). */
    void setOverlay (bool);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Click (not when locked): close the tuner. */
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void updateText();   // strings and colours, when the shown reading changes

    Tuner::Reading shown;
    bool muted = true, overlay = true;
    float reference = 440.0f;
    int framesWithoutNote = 0;

    // What's drawn, rebuilt only when it changes (no strings or fonts in paint()).
    juce::String note, octave, detail, caption;
    juce::uint32 noteColour = 0;
    int litTick = -1;
    float noteWidth = 0.0f;

    // Layout, from resized().
    float scale = 1.0f;
    juce::Rectangle<float> pane, lcd, noteBox, ticks, detailBox, captionBox, button;
    juce::Font noteFont { juce::FontOptions() }, octaveFont { juce::FontOptions() };
    juce::ColourGradient lcdFill, buttonFill;
};

} // namespace encore
