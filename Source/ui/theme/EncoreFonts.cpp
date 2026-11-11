// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/EncoreFonts.h"

#include <map>
#include <tuple>

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
    // Fonts are made once per (weight, size, tracking) and copied after that
    // (a copy shares the font's data), so paint() never builds a font.
    // Message thread only, like all UI.
    JUCE_ASSERT_MESSAGE_THREAD

    using Key = std::tuple<int, int, int>;
    static std::map<Key, juce::Font> cache;

    const Key key { (int) weight, juce::roundToInt (juce::jmax (1.0f, px) * 100.0f), juce::roundToInt (tracking * 1000.0f) };

    if (const auto found = cache.find (key); found != cache.end())
        return found->second;

    if (cache.size() > 1024)   // fitted text can ask for many sizes; never grow without bound
        cache.clear();

    const auto em = (float) std::get<1> (key) / 100.0f;
    juce::Font f (juce::FontOptions (EncoreFonts::get().typeface (weight)).withPointHeight (em));

    // CSS letter-spacing is a share of the em; JUCE's kerning factor is a
    // share of the font's height (ascent + descent, about 1.21 em for Inter).
    if (std::abs (tracking) > 0.0f && f.getHeight() > 0.0f)
        f = f.withExtraKerningFactor (tracking * em / f.getHeight());

    cache.emplace (key, f);
    return f;
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
