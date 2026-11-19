// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/PopupField.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

PopupField::PopupField() : juce::Button ({})
{
    setMouseClickGrabsKeyboardFocus (false);
    setTriggeredOnMouseDown (true);   // a menu opens on press, as a combo box
}

void PopupField::setText (const juce::String& newName, const juce::String& newCaption, bool isMissing)
{
    if (newName == name && newCaption == caption && isMissing == missing)
        return;

    name = newName;
    caption = newCaption;
    missing = isMissing;
    separated = caption.isEmpty() ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + caption;
    setTitle ("Input " + name + separated);
    nameWidth = textWidth (font (compact ? type::pickerCompact : type::pickerName), name) + 1.0f;
    repaint();
}

void PopupField::setCompact (bool c)
{
    if (c != compact)
    {
        compact = c;
        nameWidth = textWidth (font (compact ? type::pickerCompact : type::pickerName), name) + 1.0f;
        repaint();
    }
}

void PopupField::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat();
    const auto radius = (float) size::inputPickerRadius;
    const auto alpha = isEnabled() ? 1.0f : state::disabledAlpha;

    if (down)
        paint::pressed (g, r, radius, colour::fieldBorder);
    else
        paint::raised (g, r, colour::pickerTop, colour::pickerBot, colour::fieldBorder, radius, false, highlighted ? state::hoverLighten : 0.0f);

    const auto padX = (float) (compact ? size::mixerInputPickerPadX : size::inputPickerPadX);
    auto area = r.reduced (padX, 0.0f);
    const auto chevron = (float) size::inputPickerChevron;
    drawIcon (g, Icon::chevronDown, area.removeFromRight (chevron).withSizeKeepingCentre (chevron, chevron),
              paint::c (colour::textSecondary).withMultipliedAlpha (alpha));
    area.removeFromRight ((float) size::outGap);

    const auto& nameStyle = compact ? type::pickerCompact : type::pickerName;
    const auto nameColour = paint::c (missing ? colour::amber : colour::text).withMultipliedAlpha (alpha);
    const auto nameW = juce::jmin (area.getWidth(), nameWidth);
    g.setFont (font (nameStyle));
    g.setColour (nameColour);
    g.drawText (name, area.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    if (! compact && separated.isNotEmpty() && area.getWidth() > 8.0f)
    {
        g.setFont (font (type::pickerCaption));
        g.setColour (paint::c (colour::textMuted).withMultipliedAlpha (alpha));
        g.drawText (separated, area, juce::Justification::centredLeft, true);
    }

    if (hasKeyboardFocus (false) && getWantsKeyboardFocus())
        paint::focusRing (g, r, radius);
}

} // namespace encore::kit
