// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/LevelMeter.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace encore
{

namespace
{
    constexpr float floorDb = -60.0f;
    constexpr float fallDbPerFrame = 1.5f;   // ~45 dB/s at 30 fps
    constexpr int holdFramesMax = 45;        // ~1.5 s
    constexpr int clipFramesMax = 60;        // ~2 s
}

LevelMeter::LevelMeter (juce::String labelText) : label (std::move (labelText)) {}

float LevelMeter::toProportion (float decibels) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (decibels - floorDb) / -floorDb);
}

void LevelMeter::setPeak (float linearPeak)
{
    const auto peakDb = juce::Decibels::gainToDecibels (linearPeak, -100.0f);

    displayDb = juce::jmax (peakDb, displayDb - fallDbPerFrame);

    if (peakDb >= holdDb || --holdFrames <= 0)
    {
        holdDb = peakDb;
        holdFrames = holdFramesMax;
    }

    if (linearPeak >= 0.99f)
        clipFrames = clipFramesMax;
    else if (clipFrames > 0)
        --clipFrames;

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (label, area.removeFromLeft (90.0f), juce::Justification::centredLeft);

    auto clipLight = area.removeFromRight (28.0f).reduced (4.0f);
    area.removeFromRight (6.0f);

    g.setColour (juce::Colour (0xff1c1f24));
    g.fillRoundedRectangle (area, 4.0f);

    const auto width = area.getWidth();
    const auto level = area.withWidth (width * toProportion (displayDb));

    // Green up to -18 dBFS, amber to -6, red above.
    juce::ColourGradient gradient (juce::Colour (0xff3ccf6e), area.getX(), 0.0f,
                                   juce::Colour (0xffe5484d), area.getRight(), 0.0f, false);
    gradient.addColour (toProportion (-18.0f), juce::Colour (0xff3ccf6e));
    gradient.addColour (toProportion (-6.0f), juce::Colour (0xfff0b429));
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (level, 4.0f);

    if (holdDb > floorDb)
    {
        const auto x = area.getX() + width * toProportion (holdDb);
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));
    }

    g.setColour (clipFrames > 0 ? juce::Colour (0xffe5484d) : juce::Colour (0xff2a2e35));
    g.fillEllipse (clipLight.withSizeKeepingCentre (16.0f, 16.0f));
}

} // namespace encore
