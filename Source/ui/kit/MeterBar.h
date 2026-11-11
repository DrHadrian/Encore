// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A level meter (DESIGN-SYSTEM.md §5.7, the restyled LevelMeter), fed by
    the owner's timer with the peak taken from a PeakMeter. A row of three
    columns: label (50), 8 px track, value (54, "−9.4 dB", or "muted" in
    red). The fill is green to 80 % of its length, then yellow; a 2 px mark
    holds the peak. Ballistics are LevelMeter's: falls ~45 dB/s, holds ~1.5 s;
    a clip (peak at 0 dBFS) turns the mark and the value red for ~2 s, as
    LevelMeter's clip light did. Only the track and the value are
    repainted, and only when they change. */
class MeterBar final : public juce::Component
{
public:
    explicit MeterBar (const juce::String& label);

    /** UI thread: the newest block peak (linear). */
    void setPeak (float linearPeak);

    /** Shows "muted" (and no level) instead of the value. */
    void setMuted (bool);

    bool isShowingClip() const noexcept { return clipFrames > 0; }

    /** The level the bar shows now (dB, after the fall-back). */
    float getDisplayDb() const noexcept { return displayDb; }

    /** Where a level sits on the track, 0..1 (the scale's labels: −60 −48
        −36 −24 −12 −6 0, the last three steps wider than linear dB). */
    static float toProportion (float decibels) noexcept;

    /** The dB scale for a track spanning `track` horizontally, laid out as
        on the board (labels spread from the track's left to its right edge,
        equal gaps between them), in `row` (9 px tabular, #6A6A71). */
    static void paintScale (juce::Graphics&, juce::Rectangle<float> track, juce::Rectangle<float> row);

    /** The track inside this component (to line up a scale under it). */
    juce::Rectangle<float> getTrackBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> valueBounds() const;

    juce::String label, valueText;
    float displayDb = -100.0f, holdDb = -100.0f;
    int holdFrames = 0, clipFrames = 0;
    bool muted = false;
    juce::Path trackShape;
};

/** The dB scale as a component, for a pane that holds meters: give it the
    track's horizontal span (in its own coordinates). */
class MeterScale final : public juce::Component
{
public:
    MeterScale() { setInterceptsMouseClicks (false, false); }

    void setTrack (float x, float width)
    {
        if (! juce::approximatelyEqual (x, trackX) || ! juce::approximatelyEqual (width, trackW))
        {
            trackX = x;
            trackW = width;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto row = getLocalBounds().toFloat();
        MeterBar::paintScale (g, { trackX, row.getY(), trackW, row.getHeight() }, row);
    }

private:
    float trackX = 0.0f, trackW = 0.0f;
};

} // namespace encore::kit
