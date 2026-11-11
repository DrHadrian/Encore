// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/EncoreFonts.h"

#include "EncoreUiAssets.h"

namespace encore::theme
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
}

EncoreFonts::EncoreFonts()
    : regular   (load (EncoreUiAssets::InterRegular_ttf,   EncoreUiAssets::InterRegular_ttfSize)),
      medium    (load (EncoreUiAssets::InterMedium_ttf,    EncoreUiAssets::InterMedium_ttfSize)),
      semibold  (load (EncoreUiAssets::InterSemiBold_ttf,  EncoreUiAssets::InterSemiBold_ttfSize)),
      bold      (load (EncoreUiAssets::InterBold_ttf,      EncoreUiAssets::InterBold_ttfSize)),
      extraBold (load (EncoreUiAssets::InterExtraBold_ttf, EncoreUiAssets::InterExtraBold_ttfSize))
{
}

EncoreFonts::~EncoreFonts()
{
    clearSingletonInstance();
}

EncoreFonts& EncoreFonts::get()
{
    return *getInstance();
}

juce::Typeface::Ptr EncoreFonts::typeface (type::Weight weight) const
{
    switch (weight)
    {
        case type::Weight::medium:     return medium;
        case type::Weight::semibold:   return semibold;
        case type::Weight::bold:       return bold;
        case type::Weight::extraBold:  return extraBold;
        case type::Weight::regular:    break;
    }

    return regular;
}

juce::Font fontOfSize (type::Weight weight, float px, float tracking)
{
    return juce::Font (juce::FontOptions (EncoreFonts::get().typeface (weight))
                           .withPointHeight (juce::jmax (1.0f, px))
                           .withKerningFactor (tracking));
}

juce::Font font (const type::Style& style, float scale)
{
    return fontOfSize (style.weight, style.px * scale, style.tracking);
}

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

float fittedSize (type::Weight weight, const juce::String& text, float width, float maxPx, float minPx, float tracking)
{
    if (text.isEmpty() || width <= 0.0f)
        return maxPx;

    // Width grows linearly with the size: measure once at 100 and scale.
    const auto at100 = textWidth (fontOfSize (weight, 100.0f, tracking), text);

    if (at100 <= 0.0f)
        return maxPx;

    return juce::jlimit (juce::jmin (minPx, maxPx), maxPx, 100.0f * width / at100);
}

} // namespace encore::theme
