// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <tuple>

#include "ui/theme/EncoreTheme.h"

namespace encore::theme
{

/** The bundled Inter typefaces, loaded once (message thread) and kept until
    JUCE shuts down. Fonts never come from the system and text never depends
    on a fallback font. */
class EncoreFonts final : private juce::DeletedAtShutdown
{
public:
    ~EncoreFonts() override;

    static EncoreFonts& get();

    juce::Typeface::Ptr typeface (type::Weight) const;

    /** fontOfSize()'s made fonts: (weight, px x 100, tracking x 1000). Kept
        here so they go when JUCE shuts down, before its leak checks. */
    std::map<std::tuple<int, int, int>, juce::Font>& sizedFonts() noexcept { return cache; }

    JUCE_DECLARE_SINGLETON_INLINE (EncoreFonts, false)

private:
    EncoreFonts();

    juce::Typeface::Ptr regular, medium, semibold, bold, extraBold;
    std::map<std::tuple<int, int, int>, juce::Font> cache;
};

/** A token style as a JUCE font, at `scale` times its size. Sizes are the em
    size (point height in JUCE 9, see docs/design/JUCE-ARCHITECTURE.md §8).
    Uppercase styles carry their tracking; callers pass upper-case text. */
juce::Font font (const type::Style& style, float scale = 1.0f);

/** A font of a given weight and em size (for text that is fitted to a box). */
juce::Font fontOfSize (type::Weight weight, float px, float tracking = 0.0f);

/** Width of a line of text in a font (for fitting and layout, not in paint()). */
float textWidth (const juce::Font& font, const juce::String& text);

/** The biggest em size (up to maxPx) at which text fits `width` on one line. */
float fittedSize (type::Weight weight, const juce::String& text, float width, float maxPx, float minPx = 8.0f,
                  float tracking = 0.0f);

} // namespace encore::theme
