// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <array>
#include <span>

#include "engine/dsp/Svf.h"

namespace encore::dsp
{

/** The shapes a filter band can take (the Encore Engine's DSP, decision 0062). */
enum class FilterShape
{
    bell,        // decramped (matched to the analog prototype)
    lowShelf,    // decramped
    highShelf,   // decramped
    lowCut,      // Butterworth, 6..96 dB/oct
    highCut,     // Butterworth, 6..96 dB/oct
    notch,
    bandPass,
    allPass
};

/** What a band is set to. Plain data, any thread. */
struct FilterSettings
{
    FilterShape shape = FilterShape::bell;
    double frequency = 1000.0;     // Hz (clamped below Nyquist)
    double gainDb = 0.0;           // bells and shelves
    double q = 0.7071;             // bells, shelves, notch, band pass, all pass
    int slopeDbPerOctave = 12;     // cuts: 6, 12 ... 96 (multiples of 6)

    bool operator== (const FilterSettings&) const = default;
};

/** A band designed for one sample rate: up to 8 second-order sections
    (a 96 dB/oct cut is a 16th-order Butterworth).

    This is the one response function the audio and the UI share: the band's
    sections are what FilterBand plays, and magnitudeDb() is computed from
    the same numbers, so a curve on screen equals what you hear.

    Plain maths: no allocation, any thread.
*/
struct FilterBandDesign
{
    static constexpr int maxSections = 8;
    static constexpr int maxSlopeDbPerOctave = 96;
    static constexpr double maxGainDb = 60.0;    // a safety limit, beyond any module's range

    std::array<BiquadDesign, (size_t) maxSections> sections {};
    int numSections = 0;            // 0 = the band changes nothing (e.g. a bell at 0 dB)

    static FilterBandDesign make (const FilterSettings& settings, double sampleRate) noexcept;

    /** The band's magnitude in dB at a frequency. */
    double magnitudeDb (double frequency, double sampleRate) const noexcept;
};

/** The magnitude in dB of a set of bands in series. Any thread. */
double responseDb (std::span<const FilterBandDesign> bands, double frequency, double sampleRate) noexcept;

/** Plays a FilterBandDesign on TPT state-variable filters, in double.

    setDesign() may be called on the audio thread at any time, e.g. every
    16 samples while a value moves: the filters keep their state, so a fast
    sweep stays smooth and stable. Audio thread: no allocation, no locks.
*/
template <int MaxChannels = 2>
class FilterBand
{
public:
    void setDesign (const FilterBandDesign& newDesign) noexcept
    {
        design = newDesign;

        for (int s = 0; s < design.numSections; ++s)
            coefficients[(size_t) s] = SvfCoefficients::fromDesign (design.sections[(size_t) s]);
    }

    const FilterBandDesign& getDesign() const noexcept { return design; }

    void reset() noexcept
    {
        for (auto& f : filters)
            f.reset();
    }

    float processSample (float x, int channel) noexcept
    {
        auto y = (double) x;

        for (int s = 0; s < design.numSections; ++s)
            y = filters[(size_t) s].tick (y, channel, coefficients[(size_t) s]);

        return (float) y;
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (design.numSections == 0)
            return;   // bit-exact passthrough

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                channels[ch][i] = processSample (channels[ch][i], ch);
    }

private:
    FilterBandDesign design;
    std::array<SvfCoefficients, (size_t) FilterBandDesign::maxSections> coefficients {};
    std::array<Svf<MaxChannels>, (size_t) FilterBandDesign::maxSections> filters {};
};

} // namespace encore::dsp
