// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace encore::kit
{

/** The mixer view's meter law (V3.2-CHANGES §3.3): where a level sits on a
    vertical dBFS meter, from the bottom: 0 dB → 1, −6 → 0.82, −12 → 0.64,
    −24 → 0.38, −48 → 0.08, −60 → 0, linear between. */
namespace dbfsLaw
{
    float positionFor (float dB) noexcept;   // 0 (bottom, −60 and below) .. 1 (0 dBFS and above)

    constexpr float floorDb = -60.0f;        // the meter's bottom: quieter reads "—"

    /** A level as the mixer view writes it: "−8.2", "0.0", "+2.5"; −∞ at or below `silentDb`. */
    juce::String readout (float dB, float silentDb);

    /** The scale beside a meter: 0, −6, −12, −24, −48, 9 px #8C8C93, right-aligned
        in `column`, each centred on its line of a meter spanning `meter` (heights). */
    void paintScale (juce::Graphics&, juce::Rectangle<float> column, juce::Rectangle<float> meter);
}

/** A vertical dBFS meter (the mixer view's): one or two 5 px bars, 2 apart,
    track #0E0E10, filled from the bottom with fixed colour zones over the
    whole height (green to 80 %, yellow 90–97 %, red from 98.5 %), and a 1 px
    peak tick. Fed by the owner's 30 Hz timer with linear peaks; falls like
    the other meters and holds the tick ~1.5 s. Repaints only on change. */
class DbfsMeter final : public juce::Component
{
public:
    DbfsMeter();

    void setNumChannels (int);   // 1 or 2
    void setMuted (bool);        // empty bars (a muted output plays nothing)

    /** UI thread: the newest peaks (linear); right is ignored for one channel. */
    void setPeaks (float left, float right);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> barBounds (int channel) const;

    int channels = 2;
    bool muted = false;
    std::array<float, 2> displayDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f };
    std::array<int, 2> holdFrames { 0, 0 };
    juce::ColourGradient zones;   // made when the size changes
};

} // namespace encore::kit
