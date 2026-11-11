// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/MeterBar.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr float floorDb = -60.0f;
    constexpr float fallDbPerFrame = 1.5f;   // ~45 dB/s at 30 fps (as LevelMeter)
    constexpr int holdFramesMax = 45;        // ~1.5 s

    const juce::String& minus()
    {
        static const juce::String sign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));   // displays use a real minus
        return sign;
    }

    juce::String dbText (float db)
    {
        if (db <= floorDb)
            return minus() + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) + " dB";   // "−∞ dB": silence

        const auto rounded = std::round (db * 10.0f) / 10.0f;
        return (rounded < 0.0f ? minus() : juce::String()) + juce::String (std::abs (rounded), 1) + " dB";
    }

    // The scale's labels and their widths, measured once.
    struct ScaleLabels
    {
        juce::StringArray text;
        float widths[7] {};
        float total = 0.0f;

        ScaleLabels()
        {
            const auto f = font (type::meterScale);
            const int marks[] = { -60, -48, -36, -24, -12, -6, 0 };

            for (int i = 0; i < 7; ++i)
            {
                text.add (marks[i] < 0 ? minus() + juce::String (-marks[i]) : juce::String ("0"));
                widths[i] = textWidth (f, text[i]);
                total += widths[i];
            }
        }
    };

    const ScaleLabels& scaleLabels()
    {
        static const ScaleLabels labels;
        return labels;
    }

    // The green-to-yellow end of the fill as a small image, made once and
    // stretched, so a meter's per-frame repaint builds no gradient.
    const juce::Image& fadeStrip()
    {
        static const juce::Image strip = []
        {
            juce::Image image (juce::Image::ARGB, 64, 1, false);

            for (int x = 0; x < image.getWidth(); ++x)
                image.setPixelAt (x, 0, paint::c (colour::meterGreen)
                                            .interpolatedWith (paint::c (colour::meterYellow), (float) x / (float) (image.getWidth() - 1)));

            return image;
        }();

        return strip;
    }
}

MeterBar::MeterBar (const juce::String& l) : label (l), valueText (dbText (floorDb))
{
    setInterceptsMouseClicks (false, false);
    fadeStrip();     // made here, not in the first paint()
    scaleLabels();
}

float MeterBar::toProportion (float db) noexcept
{
    // −60…−12 in four equal steps, then −12…−6 and −6…0 one step each.
    constexpr float step = 1.0f / 6.0f;

    if (db <= floorDb)  return 0.0f;
    if (db <= -12.0f)   return (db - floorDb) / 12.0f * step;
    if (db <= -6.0f)    return 4.0f * step + (db + 12.0f) / 6.0f * step;
    return juce::jmin (1.0f, 5.0f * step + (db + 6.0f) / 6.0f * step);
}

void MeterBar::setPeak (float linearPeak)
{
    const auto peakDb = juce::Decibels::gainToDecibels (linearPeak, -100.0f);
    const auto oldFill = toProportion (displayDb), oldHold = toProportion (holdDb);

    displayDb = juce::jmax (peakDb, displayDb - fallDbPerFrame);

    if (peakDb >= holdDb || --holdFrames <= 0)
    {
        holdDb = peakDb;
        holdFrames = holdFramesMax;
    }

    const auto track = getTrackBounds();

    // Repaint the track only when the fill or the mark moved by a pixel or more.
    if (std::abs (toProportion (displayDb) - oldFill) * track.getWidth() >= 0.5f
        || std::abs (toProportion (holdDb) - oldHold) * track.getWidth() >= 0.5f)
        repaint (track.expanded (1.0f).getSmallestIntegerContainer());

    if (! muted)
    {
        auto text = dbText (holdDb);

        if (text != valueText)
        {
            valueText = std::move (text);
            repaint (valueBounds().getSmallestIntegerContainer());
        }
    }
}

void MeterBar::setMuted (bool shouldBeMuted)
{
    if (shouldBeMuted != muted)
    {
        muted = shouldBeMuted;
        valueText = muted ? juce::String ("muted") : dbText (holdDb);
        repaint();
    }
}

juce::Rectangle<float> MeterBar::getTrackBounds() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft ((float) (size::meterLabelW + size::meterGap));
    r.removeFromRight ((float) (size::meterValueW + size::meterGap));
    return r.withSizeKeepingCentre (juce::jmax (0.0f, r.getWidth()), (float) size::meterTrackH);
}

juce::Rectangle<float> MeterBar::valueBounds() const
{
    return getLocalBounds().toFloat().removeFromRight ((float) size::meterValueW);
}

void MeterBar::resized()
{
    trackShape.clear();
    trackShape.addRoundedRectangle (getTrackBounds(), (float) size::meterTrackH * 0.5f);
}

void MeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paint::text (g, label, r.removeFromLeft ((float) size::meterLabelW), type::label, colour::textTertiary);
    paint::text (g, valueText, valueBounds(), type::meterValue, muted ? colour::danger : colour::text,
                 juce::Justification::centredRight);

    const auto track = getTrackBounds();
    g.setColour (paint::c (colour::meterTrack));
    g.fillPath (trackShape);

    if (muted)
        return;

    // The fill and the mark are clipped to the rounded track (overflow: hidden).
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (trackShape);

    if (displayDb > floorDb)
    {
        // Green for the first 80 % of the fill's own length, then fading to yellow.
        const auto fill = track.withWidth (track.getWidth() * toProportion (displayDb));
        const auto greenEnd = fill.getX() + fill.getWidth() * 0.8f;
        g.setColour (paint::c (colour::meterGreen));
        g.fillRect (fill.withRight (greenEnd));
        g.drawImage (fadeStrip(), fill.withLeft (greenEnd), juce::RectanglePlacement::stretchToFit);
    }

    if (holdDb > floorDb)
    {
        const auto x = juce::jmin (track.getRight() - (float) size::meterPeakW, track.getX() + track.getWidth() * toProportion (holdDb));
        g.setColour (paint::c (colour::meterPeak));
        g.fillRect (juce::Rectangle<float> (x, track.getY(), (float) size::meterPeakW, track.getHeight()));
    }
}

void MeterBar::paintScale (juce::Graphics& g, juce::Rectangle<float> track, juce::Rectangle<float> row)
{
    const auto& labels = scaleLabels();
    const auto gap = juce::jmax (0.0f, (track.getWidth() - labels.total) / 6.0f);
    auto x = track.getX();

    g.setFont (font (type::meterScale));
    g.setColour (paint::c (colour::meterScale));

    for (int i = 0; i < 7; ++i)
    {
        g.drawText (labels.text[i], juce::Rectangle<float> (x, row.getY(), labels.widths[i] + 1.0f, row.getHeight()),
                    juce::Justification::centredLeft, false);
        x += labels.widths[i] + gap;
    }
}

} // namespace encore::kit
