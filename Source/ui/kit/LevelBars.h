// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace encore::kit
{

/** Bare level bars (the stereo variant of MeterBar, NEW-FEATURES-M10-M11 §C):
    one or two thin tracks (#0E0E10, round ends), each filled from the start
    to the level, green over most of the fill, yellow towards its end; an
    optional 2 px peak-hold mark. Horizontal (outputs, inputs) or vertical
    (beside a send fader). The scale is MeterBar's (−60 … 0 dB).

    Fed by the owner's UI timer with linear peaks (as MeterBar); falls
    ~45 dB/s, holds the peak mark ~1.5 s. Repaints only when a bar moves. */
class LevelBars final : public juce::Component
{
public:
    enum class Orientation { horizontal, vertical };

    explicit LevelBars (Orientation = Orientation::horizontal, int numChannels = 2);

    void setNumChannels (int);              // 1 or 2
    void setBarThickness (int px);          // default 4
    void setGap (int px);                   // between the two tracks, default 2
    void setGreenShare (float);             // of the fill that stays green (0.8)
    void setShowsPeak (bool);
    void setMuted (bool);                   // empty tracks (what a muted output plays)

    /** UI thread: the newest peaks (linear); right is ignored for one channel. */
    void setPeaks (float left, float right);

    float getDisplayDb (int channel) const noexcept { return displayDb[(size_t) juce::jlimit (0, 1, channel)]; }

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> trackBounds (int channel) const;

    Orientation orientation;
    int channels, thickness = 4, gap = 2;
    float greenShare = 0.8f;
    bool showsPeak = false, muted = false;
    std::array<float, 2> displayDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f };
    std::array<int, 2> holdFrames { 0, 0 };
};

} // namespace encore::kit
