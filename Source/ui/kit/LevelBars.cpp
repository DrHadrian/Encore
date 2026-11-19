// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/LevelBars.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ui/kit/MeterBar.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    // MeterBar's ballistics at the 30 Hz UI timer.
    constexpr float fallDbPerFrame = 1.5f;
    constexpr int holdFramesMax = 45;
}

LevelBars::LevelBars (Orientation o, int numChannels) : orientation (o), channels (juce::jlimit (1, 2, numChannels))
{
    setInterceptsMouseClicks (false, false);
    setPaintingIsUnclipped (false);
}

void LevelBars::setNumChannels (int n)
{
    n = juce::jlimit (1, 2, n);

    if (n != channels)
    {
        channels = n;
        repaint();
    }
}

void LevelBars::setBarThickness (int px)  { thickness = juce::jmax (1, px); repaint(); }
void LevelBars::setGap (int px)           { gap = juce::jmax (0, px); repaint(); }
void LevelBars::setGreenShare (float s)   { greenShare = juce::jlimit (0.0f, 1.0f, s); repaint(); }
void LevelBars::setShowsPeak (bool s)     { showsPeak = s; repaint(); }

void LevelBars::setMuted (bool m)
{
    if (m != muted)
    {
        muted = m;
        repaint();
    }
}

void LevelBars::setPeaks (float left, float right)
{
    const auto length = orientation == Orientation::horizontal ? (float) getWidth() : (float) getHeight();
    auto changed = false;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto peakDb = juce::Decibels::gainToDecibels (ch == 0 ? left : right, -100.0f);
        auto& shown = displayDb[(size_t) ch];
        auto& hold = holdDb[(size_t) ch];
        const auto oldFill = MeterBar::toProportion (shown), oldHold = MeterBar::toProportion (hold);

        shown = juce::jmax (peakDb, shown - fallDbPerFrame);

        if (peakDb >= hold || --holdFrames[(size_t) ch] <= 0)
        {
            hold = peakDb;
            holdFrames[(size_t) ch] = holdFramesMax;
        }

        if (std::abs (MeterBar::toProportion (shown) - oldFill) * length >= 0.5f
            || (showsPeak && std::abs (MeterBar::toProportion (hold) - oldHold) * length >= 0.5f))
            changed = true;
    }

    if (changed && ! muted)
        repaint();
}

juce::Rectangle<float> LevelBars::trackBounds (int channel) const
{
    const auto r = getLocalBounds().toFloat();
    const auto t = (float) thickness, g = (float) gap;
    const auto span = (float) channels * t + (float) (channels - 1) * g;

    if (orientation == Orientation::horizontal)
    {
        const auto top = r.getY() + (r.getHeight() - span) * 0.5f;
        return { r.getX(), top + (float) channel * (t + g), r.getWidth(), t };
    }

    const auto left = r.getX() + (r.getWidth() - span) * 0.5f;
    return { left + (float) channel * (t + g), r.getY(), t, r.getHeight() };
}

void LevelBars::paint (juce::Graphics& g)
{
    const auto radius = (float) thickness * 0.5f;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto track = trackBounds (ch);
        g.setColour (paint::c (colour::meterTrack));
        g.fillRoundedRectangle (track, radius);

        if (muted)
            continue;

        const auto p = MeterBar::toProportion (displayDb[(size_t) ch]);

        if (p > 0.0f)
        {
            // Green over most of the fill, yellow towards its end (the gradient spans the fill).
            juce::Rectangle<float> fill;
            juce::ColourGradient gradient;

            if (orientation == Orientation::horizontal)
            {
                fill = track.withWidth (track.getWidth() * p);
                gradient = juce::ColourGradient (paint::c (colour::meterGreen), fill.getX(), 0.0f,
                                                 paint::c (colour::meterYellow), fill.getRight(), 0.0f, false);
            }
            else
            {
                fill = track.withTop (track.getBottom() - track.getHeight() * p);
                gradient = juce::ColourGradient (paint::c (colour::meterGreen), 0.0f, fill.getBottom(),
                                                 paint::c (colour::meterYellow), 0.0f, fill.getY(), false);
            }

            gradient.addColour (greenShare, paint::c (colour::meterGreen));
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (fill, radius);
        }

        if (showsPeak && holdDb[(size_t) ch] > -100.0f)
        {
            const auto hp = MeterBar::toProportion (holdDb[(size_t) ch]);
            g.setColour (paint::c (colour::meterPeak));

            if (orientation == Orientation::horizontal)
                g.fillRect (juce::Rectangle<float> ((float) size::meterPeakW, track.getHeight()).withPosition (track.getX() + track.getWidth() * hp - 1.0f, track.getY()));
            else
                g.fillRect (juce::Rectangle<float> (track.getWidth(), (float) size::meterPeakW).withPosition (track.getX(), track.getBottom() - track.getHeight() * hp - 1.0f));
        }
    }
}

} // namespace encore::kit
