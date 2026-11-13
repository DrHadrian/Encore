// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/FaultCard.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

FaultCard::FaultCard()
{
    setVariant (primary, Variant::primary);

    for (auto* b : { &primary, &secondary })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addChildComponent (b);
    }
}

int FaultCard::getShadowMargin()
{
    return size::faultCardShadowBlur + size::faultCardShadowY;
}

juce::Rectangle<int> FaultCard::boundsFor (juce::Rectangle<int> body)
{
    return body.expanded (getShadowMargin());
}

juce::Rectangle<int> FaultCard::getBody() const
{
    return getLocalBounds().reduced (getShadowMargin());
}

void FaultCard::setMessage (const juce::String& newLead, const juce::String& newRest)
{
    if (newLead == lead && newRest == rest)
        return;

    lead = newLead;
    rest = newRest;
    setTitle (lead + " " + rest);
    layOutText();
    repaint();
}

void FaultCard::setActions (const juce::String& primaryText, const juce::String& secondaryText)
{
    primary.setButtonText (primaryText);
    primary.setVisible (primaryText.isNotEmpty());
    secondary.setButtonText (secondaryText);
    secondary.setVisible (secondaryText.isNotEmpty());
    resized();
}

juce::AttributedString FaultCard::makeText() const
{
    juce::AttributedString s;
    s.setLineSpacing ((float) size::faultCardLineGap);
    s.setWordWrap (juce::AttributedString::byWord);

    if (lead.isNotEmpty())
        s.append (lead + " ", font (type::faultStrong), paint::c (colour::faultCardText));

    s.append (rest, font (type::faultText), paint::c (colour::faultCardText));
    return s;
}

namespace
{
    int textWidthFor (int bodyWidth)
    {
        return bodyWidth - 2 * size::faultCardPadX - size::faultCardIconW - size::faultCardIconGap;
    }
}

int FaultCard::getBodyHeight (int bodyWidth) const
{
    juce::TextLayout layout;
    layout.createLayout (makeText(), (float) juce::jmax (1, textWidthFor (bodyWidth)));
    const auto buttons = primary.isVisible() || secondary.isVisible() ? size::faultCardGap + size::buttonHeight : 0;
    // Each line is as tall as its spacing (CSS line-height): half the gap above the first, half under the last.
    return 2 * size::faultCardPadY + juce::roundToInt (layout.getHeight()) + size::faultCardLineGap + buttons;
}

void FaultCard::layOutText()
{
    text.createLayout (makeText(), (float) juce::jmax (1, textWidthFor (getBody().getWidth())));
}

void FaultCard::resized()
{
    layOutText();

    const auto body = getBody();
    const auto y = body.getBottom() - size::faultCardPadY - size::buttonHeight;
    auto x = body.getX() + size::faultCardPadX;

    for (auto* b : { &primary, &secondary })
    {
        if (! b->isVisible())
            continue;

        const auto w = b->getBestWidthForHeight (size::buttonHeight);
        b->setBounds (x, y, w, size::buttonHeight);
        x += w + size::faultCardButtonGap;
    }
}

void FaultCard::paint (juce::Graphics& g)
{
    const auto body = getBody();

    if (body.isEmpty())
        return;

    // The drop shadow (0, 14, 30 px, black 50 %), rendered once per size and screen scale.
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();

    if (shadow.isNull() || shadowFor != getLocalBounds() || ! juce::approximatelyEqual (scale, shadowScale))
    {
        shadowFor = getLocalBounds();
        shadowScale = scale;
        shadow = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                              juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics sg (shadow);
        sg.addTransform (juce::AffineTransform::scale (scale));
        juce::Path shape;
        shape.addRoundedRectangle (body.toFloat(), (float) size::faultCardRadius);
        juce::DropShadow (paint::c (colour::faultCardShadow), size::faultCardShadowBlur, { 0, size::faultCardShadowY }).drawForPath (sg, shape);
    }

    g.drawImage (shadow, getLocalBounds().toFloat());

    const auto r = body.toFloat();
    g.setColour (paint::c (colour::faultCardBg));
    g.fillRoundedRectangle (r, (float) size::faultCardRadius);
    g.setColour (paint::c (colour::faultCardBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), (float) size::faultCardRadius - 0.5f, 1.0f);

    const auto x = r.getX() + (float) size::faultCardPadX;
    const auto top = r.getY() + (float) size::faultCardPadY;
    drawIcon (g, Icon::warn, { x, top, (float) size::faultCardIconW, (float) size::faultCardIconH }, paint::c (colour::amber));

    const auto textX = x + (float) (size::faultCardIconW + size::faultCardIconGap);
    const auto textTop = top + (float) size::faultCardLineGap * 0.5f;
    text.draw (g, { textX, textTop, r.getRight() - (float) size::faultCardPadX - textX, text.getHeight() });
}

} // namespace encore::kit
