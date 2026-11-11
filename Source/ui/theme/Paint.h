// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/EncoreTheme.h"

namespace encore::theme
{

/** The depth recipes of the design (DESIGN-SYSTEM.md §4), shared by the
    LookAndFeel and the kit so every raised control, well and pane is drawn the
    same way. They only draw: no allocation beyond JUCE's own path work. */
namespace paint
{
    inline juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    /** Vertical gradient from top to bottom colour over `r`. */
    void verticalGradient (juce::Graphics&, juce::Rectangle<float> r, juce::uint32 top, juce::uint32 bottom, float radius);

    /** A raised control: gradient, 1 px border, 1 px inner top highlight and
        an optional 1 px drop line under it. `lighten` brightens it (hover). */
    void raised (juce::Graphics&, juce::Rectangle<float> r, juce::uint32 top, juce::uint32 bottom, juce::uint32 border,
                 float radius, bool dropLine = true, float lighten = 0.0f, juce::uint32 highlight = colour::buttonHighlight);

    /** A pressed / toggled-on control: pressed gradient and an inner shadow. */
    void pressed (juce::Graphics&, juce::Rectangle<float> r, float radius, juce::uint32 border = colour::buttonBorder);

    /** An inset well (fields, displays): flat fill, border, inner top shadow. */
    void inset (juce::Graphics&, juce::Rectangle<float> r, juce::uint32 fill, juce::uint32 border, float radius,
                juce::uint32 shadow = 0x99000000);

    /** A pane (fill, border, radius, 1 px top highlight). */
    void pane (juce::Graphics&, juce::Rectangle<float> r);

    /** A pane header strip (gradient + line under it). `r` is the strip. */
    void paneHeader (juce::Graphics&, juce::Rectangle<float> r, float topRadius);

    /** Text as a style shows it: upper case for caps styles. Call this when
        the text changes (setters), not in paint(). */
    juce::String caps (const type::Style&, const juce::String&);

    /** Single line of text in a token style (already passed through caps()). */
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float> r, const type::Style&, juce::uint32 colour,
               juce::Justification = juce::Justification::centredLeft, float scale = 1.0f);

    /** A 1 px horizontal line (highlights, rules); nothing when `width` <= 0. */
    inline void hline (juce::Graphics& g, float x, float y, float width)
    {
        if (width > 0.0f)
            g.fillRect (x, y, width, 1.0f);
    }

    /** The focus ring: 2 px accent, 1 px outside `r`. */
    void focusRing (juce::Graphics&, juce::Rectangle<float> r, float radius);

    /** A tick (✓) or cross (✕) drawn as a path, in `r`. */
    void tick (juce::Graphics&, juce::Rectangle<float> r, juce::Colour, float thickness);
    void cross (juce::Graphics&, juce::Rectangle<float> r, juce::Colour, float thickness);
}

} // namespace encore::theme
