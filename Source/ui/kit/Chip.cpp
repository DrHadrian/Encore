// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Chip.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    void coloursFor (ChipStyle style, juce::uint32& fill, juce::uint32& ink)
    {
        switch (style)
        {
            case ChipStyle::mute:  fill = colour::chipMuteBg;  ink = colour::chipMuteText;  return;
            case ChipStyle::amber: fill = colour::chipAmberBg; ink = colour::chipAmberText; return;
            case ChipStyle::exp:   fill = colour::chipExpBg;   ink = colour::chipExpText;   return;
            case ChipStyle::off:   fill = colour::chipOffBg;   ink = colour::chipOffText;   return;
            case ChipStyle::green: fill = colour::chipGreenBg; ink = colour::chipGreenText; return;
            case ChipStyle::plain: break;
        }

        fill = colour::chipPlainBg;
        ink = colour::chipPlainText;
    }
}

float chipWidth (const juce::String& text)
{
    const auto shown = paint::caps (type::tagChip, text);
    const auto glyphs = textWidth (font (type::tagChip), shown);
    return juce::jmax ((float) size::tagChipH, std::ceil (glyphs) + 2.0f * (float) size::tagChipPadX);
}

void paintChip (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, ChipStyle style)
{
    juce::uint32 fill = 0, ink = 0;
    coloursFor (style, fill, ink);

    const auto r = bounds.withSizeKeepingCentre (bounds.getWidth(), juce::jmin (bounds.getHeight(), (float) size::tagChipH));
    g.setColour (paint::c (fill));
    g.fillRoundedRectangle (r, (float) size::tagChipRadius);
    paint::text (g, text, r, type::tagChip, ink, juce::Justification::centred);
}

Chip::Chip (ChipStyle s) : style (s)
{
    setInterceptsMouseClicks (false, false);
}

void Chip::setText (const juce::String& t)
{
    if (t != text)
    {
        text = t;
        repaint();
    }
}

void Chip::setStyle (ChipStyle s)
{
    if (s != style)
    {
        style = s;
        repaint();
    }
}

int Chip::getIdealWidth() const
{
    return (int) std::ceil (chipWidth (text));
}

void Chip::paint (juce::Graphics& g)
{
    if (text.isNotEmpty())
        paintChip (g, getLocalBounds().toFloat(), text, style);
}

} // namespace encore::kit
