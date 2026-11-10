// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/TunerDisplay.h"

namespace encore
{

namespace
{
    const auto inTune = juce::Colour (0xff3ddc84);
    const auto close = juce::Colour (0xfff0b429);
    const auto far = juce::Colour (0xffe5484d);
}

void TunerDisplay::setReading (const Tuner::Reading& reading, bool outputMuted, float referencePitch)
{
    // Hold the last note briefly when the string fades, so the display doesn't flicker.
    if (reading.valid)
    {
        shown = reading;
        framesWithoutNote = 0;
    }
    else if (++framesWithoutNote > 15)
    {
        shown = {};
    }

    muted = outputMuted;
    reference = referencePitch;
    repaint();
}

void TunerDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (onClick != nullptr && ! e.mods.isPopupMenu())
        onClick();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    if (isOpaque())
        g.fillAll (juce::Colour (0xff0b0d10));

    auto area = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (0xff101317));
    g.fillRoundedRectangle (area, 8.0f);
    area = area.reduced (area.getHeight() * 0.06f);

    const auto colour = ! shown.valid                   ? juce::Colours::white.withAlpha (0.3f)
                      : std::abs (shown.cents) <= 2.0f  ? inTune
                      : std::abs (shown.cents) <= 10.0f ? close
                                                        : far;

    // Header: status, reference.
    const auto smallText = juce::jlimit (11.0f, 28.0f, area.getHeight() * 0.08f);
    auto header = area.removeFromTop (smallText * 1.4f);
    g.setFont (juce::FontOptions (smallText));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (muted ? "TUNER  -  output muted" : "TUNER  -  output playing", header, juce::Justification::centredLeft);

    if (shown.valid)
        g.drawText (juce::String (shown.frequency, 1) + " Hz", header, juce::Justification::centred);

    g.drawText ("A = " + juce::String (reference, std::abs (reference - std::round (reference)) < 0.05f ? 0 : 1) + " Hz", header,
                juce::Justification::centredRight);

    // Needle scale along the bottom third.
    auto scale = area.removeFromBottom (area.getHeight() * 0.3f);
    auto noteArea = area;

    const auto centreX = scale.getCentreX();
    const auto halfWidth = scale.getWidth() * 0.5f;

    g.setColour (juce::Colours::white.withAlpha (0.25f));

    for (int c = -50; c <= 50; c += 10)
    {
        const auto x = centreX + halfWidth * (float) c / 50.0f;
        const auto tall = c == 0 ? 1.0f : 0.5f;
        g.fillRect (juce::Rectangle<float> (2.0f, scale.getHeight() * tall).withCentre ({ x, scale.getCentreY() }));
    }

    if (shown.valid)
    {
        const auto x = centreX + halfWidth * juce::jlimit (-1.0f, 1.0f, shown.cents / 50.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmax (6.0f, scale.getWidth() * 0.012f), scale.getHeight())
                                    .withCentre ({ x, scale.getCentreY() }), 3.0f);
    }

    // The note, huge, its octave small beside it, the cents on the right.
    const auto noteHeight = juce::jmin (noteArea.getHeight(), noteArea.getWidth() * 0.45f);
    const juce::Font noteFont (juce::FontOptions (noteHeight * 0.9f, juce::Font::bold));
    const auto name = shown.valid ? shown.getNoteName() : juce::String ("-");
    const auto nameWidth = juce::GlyphArrangement::getStringWidth (noteFont, name);

    g.setColour (colour);
    g.setFont (noteFont);
    g.drawText (name, noteArea, juce::Justification::centred);

    if (shown.valid)
    {
        const auto small = juce::jmax (12.0f, noteHeight * 0.25f);
        g.setFont (juce::FontOptions (small, juce::Font::bold));
        g.drawText (juce::String (shown.getOctave()),
                    juce::Rectangle<float> (noteArea.getCentreX() + nameWidth * 0.5f + 2.0f, noteArea.getCentreY(),
                                            small * 2.0f, noteHeight * 0.4f),
                    juce::Justification::topLeft);

        const auto cents = juce::roundToInt (shown.cents);
        const auto centsText = (cents > 0 ? "+" : "") + juce::String (cents) + " cents";
        const auto centsArea = noteArea.removeFromRight (noteArea.getWidth() * 0.25f);
        juce::Font centsFont { juce::FontOptions (small) };
        const auto width = juce::GlyphArrangement::getStringWidth (centsFont, centsText);

        if (width > centsArea.getWidth())
            centsFont = centsFont.withHeight (small * centsArea.getWidth() / width);

        g.setFont (centsFont);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (centsText, centsArea, juce::Justification::centredRight);
    }
}

} // namespace encore
