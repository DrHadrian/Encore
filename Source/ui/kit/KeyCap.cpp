// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/KeyCap.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

KeyCap::KeyCap (const juce::String& k) : key (k)
{
    setInterceptsMouseClicks (false, false);
}

int KeyCap::getIdealWidth() const
{
    return (int) std::ceil (textWidth (font (type::keyCap), key)) + size::keyCapPadX * 2 + 2;   // padding + border
}

void KeyCap::paint (juce::Graphics& g)
{
    paintKeyCap (g, getLocalBounds().toFloat(), key);
}

void paintKeyCap (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& key)
{
    g.setColour (paint::c (colour::kbdBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), (float) size::keyCapRadius, 1.0f);
    paint::text (g, key, r, type::keyCap, colour::textDisabled, juce::Justification::centred);
}

} // namespace encore::kit
