// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/Paint.h"
#include "ui/theme/EncoreFonts.h"

namespace encore::theme::paint
{

void verticalGradient (juce::Graphics& g, juce::Rectangle<float> r, juce::uint32 top, juce::uint32 bottom, float radius)
{
    g.setGradientFill (juce::ColourGradient::vertical (c (top), r.getY(), c (bottom), r.getBottom()));
    g.fillRoundedRectangle (r, radius);
}

void raised (juce::Graphics& g, juce::Rectangle<float> r, juce::uint32 top, juce::uint32 bottom, juce::uint32 border,
             float radius, bool dropLine, float lighten, juce::uint32 highlight)
{
    if (dropLine)
    {
        g.setColour (c (colour::buttonDrop));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    }

    auto topColour = c (top), bottomColour = c (bottom);

    if (lighten > 0.0f)
    {
        topColour = topColour.brighter (lighten);
        bottomColour = bottomColour.brighter (lighten);
    }

    g.setGradientFill (juce::ColourGradient::vertical (topColour, r.getY(), bottomColour, r.getBottom()));
    g.fillRoundedRectangle (r, radius);

    // Inner top highlight: a 1 px line just inside the border.
    g.setColour (c (highlight));
    g.fillRect (r.getX() + radius * 0.6f, r.getY() + 1.0f, r.getWidth() - radius * 1.2f, 1.0f);

    g.setColour (c (border));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void pressed (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::uint32 border)
{
    verticalGradient (g, r, colour::pressedTop, colour::pressedBot, radius);

    // Inner shadow at the top (0, 1, 3 px, black 70 %).
    const juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient::vertical (c (colour::pressedShadow), r.getY(),
                                                       c (colour::pressedShadow).withAlpha (0.0f), r.getY() + 4.0f));
    g.fillRect (r.withHeight (4.0f));
    g.setColour (c (border));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void inset (juce::Graphics& g, juce::Rectangle<float> r, juce::uint32 fill, juce::uint32 border, float radius, juce::uint32 shadow)
{
    g.setColour (c (fill));
    g.fillRoundedRectangle (r, radius);

    {
        const juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient::vertical (c (shadow), r.getY(), c (shadow).withAlpha (0.0f), r.getY() + 3.0f));
        g.fillRect (r.withHeight (3.0f));
    }

    g.setColour (c (border));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void pane (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto radius = (float) size::paneRadius;
    g.setColour (c (colour::pane));
    g.fillRoundedRectangle (r, radius);
    g.setColour (c (colour::paneHighlight));
    g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - radius * 2.0f, 1.0f);
    g.setColour (c (colour::paneBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void paneHeader (juce::Graphics& g, juce::Rectangle<float> r, float topRadius)
{
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), topRadius, topRadius, true, true, false, false);
    g.setGradientFill (juce::ColourGradient::vertical (c (colour::paneHeaderTop), r.getY(), c (colour::paneHeaderBot), r.getBottom()));
    g.fillPath (p);
    g.setColour (c (colour::paneHeaderLine));
    g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
}

void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const type::Style& style, juce::uint32 colour,
           juce::Justification justification, float scale)
{
    g.setColour (c (colour));
    g.setFont (font (style, scale));
    g.drawText (style.uppercase ? s.toUpperCase() : s, r, justification, true);
}

void focusRing (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (c (colour::accent));
    g.drawRoundedRectangle (r.expanded (2.0f), radius + 2.0f, (float) size::focusRing);
}

void tick (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colourToUse, float thickness)
{
    juce::Path p;
    p.startNewSubPath (r.getX() + r.getWidth() * 0.18f, r.getY() + r.getHeight() * 0.52f);
    p.lineTo (r.getX() + r.getWidth() * 0.40f, r.getY() + r.getHeight() * 0.72f);
    p.lineTo (r.getX() + r.getWidth() * 0.82f, r.getY() + r.getHeight() * 0.28f);
    g.setColour (colourToUse);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void cross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colourToUse, float thickness)
{
    const auto b = r.reduced (r.getWidth() * 0.25f, r.getHeight() * 0.25f);
    juce::Path p;
    p.startNewSubPath (b.getTopLeft());
    p.lineTo (b.getBottomRight());
    p.startNewSubPath (b.getTopRight());
    p.lineTo (b.getBottomLeft());
    g.setColour (colourToUse);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace encore::theme::paint
