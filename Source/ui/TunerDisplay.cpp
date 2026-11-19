// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/TunerDisplay.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    constexpr float inTuneCents = 2.0f, closeCents = 10.0f;
    constexpr int framesToHold = 15;   // keep the last note half a second when the string fades

    const juce::String doneText = juce::String (juce::CharPointer_UTF8 ("Done \xe2\x80\x94 back to live"));

    float lineOf (const type::Style& style, float scale)
    {
        const auto f = font (style, scale);
        return f.getAscent() + f.getDescent();
    }
}

TunerDisplay::TunerDisplay()
{
    updateText();
}

void TunerDisplay::setOverlay (bool shouldBeOverlay)
{
    if (overlay != shouldBeOverlay)
    {
        overlay = shouldBeOverlay;
        resized();
        repaint();
    }
}

void TunerDisplay::setReading (const Tuner::Reading& reading, bool outputMuted, float referencePitch)
{
    const auto before = std::make_tuple (shown.valid, shown.midiNote, juce::roundToInt (shown.cents),
                                         juce::roundToInt (shown.frequency * 10.0f), muted, juce::roundToInt (reference * 10.0f));

    // Hold the last note briefly when the string fades, so the display doesn't flicker.
    if (reading.valid)
    {
        shown = reading;
        framesWithoutNote = 0;
    }
    else if (++framesWithoutNote > framesToHold)
    {
        shown = {};
    }

    muted = outputMuted;
    reference = referencePitch;

    // Repaint only when something shown changed.
    if (before != std::make_tuple (shown.valid, shown.midiNote, juce::roundToInt (shown.cents),
                                   juce::roundToInt (shown.frequency * 10.0f), muted, juce::roundToInt (reference * 10.0f)))
    {
        updateText();
        repaint();
    }
}

void TunerDisplay::updateText()
{
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    const auto referenceText = "A = " + juce::String (reference, std::abs (reference - std::round (reference)) < 0.05f ? 0 : 1) + " Hz";

    caption = paint::caps (type::tunerCaption, muted ? "Tuner" + dot + "output muted while tuning" : "Tuner" + dot + "output playing");

    if (! shown.valid)
    {
        note = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));   // "–"
        octave = {};
        detail = referenceText;
        noteColour = colour::lcdLabel;
        litTick = -1;
        noteWidth = textWidth (noteFont, note);
        return;
    }

    const auto cents = juce::roundToInt (shown.cents);
    note = shown.getNoteName();
    octave = juce::String (shown.getOctave());
    detail = (cents > 0 ? "+" : cents < 0 ? minus : juce::String()) + juce::String (std::abs (cents)) + " cents"
             + dot + juce::String (shown.frequency, 1) + " Hz" + dot + referenceText;
    noteColour = std::abs (shown.cents) <= inTuneCents ? colour::healthy
               : std::abs (shown.cents) <= closeCents  ? colour::amber
                                                       : colour::danger;
    litTick = size::tunerTicks / 2 + juce::jlimit (-size::tunerTicks / 2, size::tunerTicks / 2,
                                                   juce::roundToInt (shown.cents / (float) size::tunerCentsPerTick));
    noteWidth = textWidth (noteFont, note);
}

void TunerDisplay::resized()
{
    const auto bounds = getLocalBounds().toFloat();

    // The board's sizes: the LCD's content and, around it in the overlay, the
    // pane with the caption and the Done button.
    const auto lcdContentH = (float) (size::tunerNote + size::tunerLcdGap + size::tunerTicksH + size::tunerLcdGap)
                             + lineOf (type::tunerDetail, 1.0f);
    const auto lcdH = lcdContentH + 2.0f * (float) size::tunerLcdPad;
    const auto innerW = (float) (size::tunerPaneW - 2 * size::tunerPad);

    if (overlay)
    {
        const auto paneH = 2.0f * (float) size::tunerPad + lineOf (type::tunerCaption, 1.0f) + (float) size::tunerGap + lcdH
                           + (float) size::tunerGap + (float) size::tunerButtonH;
        const auto margin = (float) size::tunerPad;

        // As on the board at the reference size; bigger on big screens, smaller on small ones.
        const auto grow = juce::jmax (1.0f, juce::jmin (bounds.getWidth() / (float) size::referenceWidth,
                                                       bounds.getHeight() / (float) size::referenceHeight));
        scale = juce::jmax (0.2f, juce::jmin (grow, (bounds.getWidth() - 2.0f * margin) / (float) size::tunerPaneW,
                                              (bounds.getHeight() - 2.0f * margin) / paneH));

        pane = bounds.withSizeKeepingCentre ((float) size::tunerPaneW * scale, paneH * scale);
        auto r = pane.reduced ((float) size::tunerPad * scale);
        captionBox = r.removeFromTop (lineOf (type::tunerCaption, scale));
        r.removeFromTop ((float) size::tunerGap * scale);
        lcd = r.removeFromTop (lcdH * scale);
        r.removeFromTop ((float) size::tunerGap * scale);
        const auto buttonW = textWidth (font (type::tunerButton, scale), doneText) + 2.0f * (float) size::tunerButtonPadX * scale;
        button = r.removeFromTop ((float) size::tunerButtonH * scale).withSizeKeepingCentre (buttonW, (float) size::tunerButtonH * scale);
    }
    else
    {
        // Inside a widget: the LCD fills it.
        scale = juce::jmax (0.1f, juce::jmin (bounds.getWidth() / (innerW), bounds.getHeight() / lcdH));
        pane = captionBox = button = {};
        lcd = bounds;
    }

    auto r = lcd.reduced ((float) size::tunerLcdPad * scale);
    const auto contentH = lcdContentH * scale;
    r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), contentH));
    noteBox = r.removeFromTop ((float) size::tunerNote * scale);
    r.removeFromTop ((float) size::tunerLcdGap * scale);
    ticks = r.removeFromTop ((float) size::tunerTicksH * scale);
    r.removeFromTop ((float) size::tunerLcdGap * scale);
    detailBox = r;

    noteFont = font (type::tunerNote, scale);
    octaveFont = font (type::tunerNote, scale * size::tunerOctave);
    noteWidth = textWidth (noteFont, note);
    lcdFill = juce::ColourGradient::vertical (paint::c (colour::lcdTop), lcd.getY(), paint::c (colour::lcdBot), lcd.getBottom());
    buttonFill = juce::ColourGradient::vertical (paint::c (colour::primaryTop), button.getY(), paint::c (colour::primaryBot), button.getBottom());
}

void TunerDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (onClick != nullptr && ! e.mods.isPopupMenu())
        onClick();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    if (overlay)
    {
        g.fillAll (paint::c (colour::tunerOverlay));
        paint::pane (g, pane);
        paint::text (g, caption, captionBox, type::tunerCaption, colour::textMuted, juce::Justification::centred, scale);
    }

    // The LCD panel.
    const auto radius = (float) size::lcdRadius;
    g.setGradientFill (lcdFill);
    g.fillRoundedRectangle (lcd, radius);
    g.setColour (paint::c (colour::lcdBorder));
    g.drawRoundedRectangle (lcd.reduced (0.5f), radius, 1.0f);

    // The note, its octave small after it.
    g.setColour (paint::c (noteColour));
    g.setFont (noteFont);
    g.drawText (note, noteBox, juce::Justification::centred, false);

    if (octave.isNotEmpty())
    {
        const auto top = noteBox.getCentreY() - noteFont.getAscent() * size::tunerOctaveRaise;
        g.setFont (octaveFont);
        g.drawText (octave, juce::Rectangle<float> (noteBox.getCentreX() + noteWidth * 0.5f + (float) size::tunerOctaveGap * scale, top,
                                                    noteBox.getWidth(), octaveFont.getAscent() + octaveFont.getDescent()),
                    juce::Justification::topLeft, false);
    }

    // The scale: 21 ticks, the centre one tall, every fifth taller, the nearest lit.
    const auto gap = (float) size::tunerTickGap * scale;
    const auto tickW = (ticks.getWidth() - gap * (float) (size::tunerTicks - 1)) / (float) size::tunerTicks;

    for (int i = 0; i < size::tunerTicks; ++i)
    {
        const auto centre = i == size::tunerTicks / 2;
        const auto h = (float) (centre ? size::tunerTicksH : (i % 5 == 0 ? size::tunerTickMid : size::tunerTickShort)) * scale;
        g.setColour (paint::c (i == litTick ? noteColour : centre ? colour::tunerTickCentre : colour::tunerTick));
        g.fillRoundedRectangle (juce::Rectangle<float> (ticks.getX() + (float) i * (tickW + gap), ticks.getCentreY() - h * 0.5f, tickW, h),
                                (float) size::tunerTickRadius * scale);
    }

    paint::text (g, detail, detailBox, type::tunerDetail, colour::tunerDetail, juce::Justification::centredTop, scale);

    if (overlay && ! button.isEmpty())
    {
        // Done: any click closes the tuner, so this is drawn, not a button.
        const auto buttonRadius = (float) size::tunerButtonRadius * scale;
        g.setGradientFill (buttonFill);
        g.fillRoundedRectangle (button, buttonRadius);
        g.setColour (paint::c (colour::primaryBorder));
        g.drawRoundedRectangle (button.reduced (0.5f), buttonRadius, 1.0f);
        paint::text (g, doneText, button, type::tunerButton, colour::textStrong, juce::Justification::centred, scale);
    }
}

} // namespace encore
