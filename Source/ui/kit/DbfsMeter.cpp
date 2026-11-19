// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/DbfsMeter.h"

#include "ui/theme/Paint.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr float fallDbPerFrame = 1.5f;   // ~45 dB/s at 30 Hz, as the other meters
    constexpr int holdFramesMax = 45;        // ~1.5 s
}

float dbfsLaw::positionFor (float dB) noexcept
{
    struct Point { float db, position; };
    static constexpr Point points[] { { 0.0f, 1.0f }, { -6.0f, 0.82f }, { -12.0f, 0.64f }, { -24.0f, 0.38f }, { -48.0f, 0.08f }, { -60.0f, 0.0f } };

    if (dB >= 0.0f)
        return 1.0f;

    for (size_t i = 1; i < std::size (points); ++i)
    {
        if (dB >= points[i].db)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            return b.position + (a.position - b.position) * (dB - b.db) / (a.db - b.db);
        }
    }

    return 0.0f;
}

juce::String dbfsLaw::readout (float dB, float silentDb)
{
    if (dB <= silentDb)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e"));   // −∞

    const auto rounded = std::round (dB * 10.0f) / 10.0f;
    const auto text = juce::String (std::abs (rounded), 1);

    if (rounded < 0.0f)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + text;

    return rounded > 0.0f ? "+" + text : text;
}

void dbfsLaw::paintScale (juce::Graphics& g, juce::Rectangle<float> column, juce::Rectangle<float> meter)
{
    static const juce::String labels[] { "0", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "6")),
                                         juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "12")),
                                         juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "24")),
                                         juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "48")) };
    static constexpr float dbs[] { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    const auto textH = theme::type::dbScaleLabel.px + 2.0f;

    for (size_t i = 0; i < std::size (dbs); ++i)
    {
        const auto y = meter.getBottom() - meter.getHeight() * positionFor (dbs[i]);
        paint::text (g, labels[i], { column.getX(), y - textH * 0.5f, column.getWidth(), textH },
                     theme::type::dbScaleLabel, theme::colour::textDim, juce::Justification::centredRight);
    }
}

DbfsMeter::DbfsMeter()
{
    setInterceptsMouseClicks (false, false);
}

void DbfsMeter::setNumChannels (int n)
{
    n = juce::jlimit (1, 2, n);

    if (n != channels)
    {
        channels = n;
        repaint();
    }
}

void DbfsMeter::setMuted (bool m)
{
    if (m != muted)
    {
        muted = m;
        displayDb = holdDb = { -100.0f, -100.0f };
        repaint();
    }
}

void DbfsMeter::setPeaks (float left, float right)
{
    const auto height = (float) getHeight();
    auto changed = false;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto peakDb = muted ? -100.0f : juce::Decibels::gainToDecibels (ch == 0 ? left : right, -100.0f);
        auto& shown = displayDb[(size_t) ch];
        auto& hold = holdDb[(size_t) ch];
        const auto oldFill = dbfsLaw::positionFor (shown), oldHold = dbfsLaw::positionFor (hold);

        shown = juce::jmax (peakDb, shown - fallDbPerFrame);

        if (peakDb >= hold || --holdFrames[(size_t) ch] <= 0)
        {
            hold = peakDb;
            holdFrames[(size_t) ch] = holdFramesMax;
        }

        if (std::abs (dbfsLaw::positionFor (shown) - oldFill) * height >= 0.5f
            || std::abs (dbfsLaw::positionFor (hold) - oldHold) * height >= 0.5f)
            changed = true;
    }

    if (changed)
        repaint();
}

juce::Rectangle<float> DbfsMeter::barBounds (int channel) const
{
    const auto w = (float) size::mixerBarW, g = (float) size::mixerBarGap;
    return { (float) channel * (w + g), 0.0f, w, (float) getHeight() };
}

void DbfsMeter::resized()
{
    // The zones run over the whole height: green to 80 %, yellow 90–97 %, red from 98.5 % (from the bottom).
    const auto h = (float) getHeight();
    zones = juce::ColourGradient::vertical (paint::c (colour::meterGreen), h, paint::c (colour::meterRed), 0.0f);
    zones.clearColours();
    zones.addColour (0.0, paint::c (colour::meterGreen));
    zones.addColour (0.80, paint::c (colour::meterGreen));
    zones.addColour (0.90, paint::c (colour::meterYellow));
    zones.addColour (0.97, paint::c (colour::meterYellow));
    zones.addColour (0.985, paint::c (colour::meterRed));
    zones.addColour (1.0, paint::c (colour::meterRed));
}

void DbfsMeter::paint (juce::Graphics& g)
{
    const auto radius = (float) size::mixerBarRadius;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto bar = barBounds (ch);
        g.setColour (paint::c (colour::meterTrack));
        g.fillRoundedRectangle (bar, radius);

        if (muted)
            continue;

        const auto fill = dbfsLaw::positionFor (displayDb[(size_t) ch]);

        if (fill > 0.0f)
        {
            const juce::Graphics::ScopedSaveState s (g);
            juce::Path track;
            track.addRoundedRectangle (bar, radius);
            g.reduceClipRegion (track);
            g.setGradientFill (zones);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * fill));
        }

        // The peak tick (1 px).
        if (const auto hold = dbfsLaw::positionFor (holdDb[(size_t) ch]); hold > 0.0f)
        {
            g.setColour (paint::c (colour::meterPeak));
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * hold).withHeight ((float) size::mixerPeakTickH));
        }
    }
}

} // namespace encore::kit
