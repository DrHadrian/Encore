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
    allPass,
    tiltShelf    // decramped; -gain/2 below the frequency, +gain/2 above (B3)
};

/** What a band is set to. Plain data, any thread. */
struct FilterSettings
{
    FilterShape shape = FilterShape::bell;
    double frequency = 1000.0;     // Hz (clamped below Nyquist)
    double gainDb = 0.0;           // bells and shelves
    double q = 0.7071;             // bells, shelves at 12 dB/oct, notch, band pass, all pass
    int slopeDbPerOctave = 12;     // 6, 12 ... 96 (multiples of 6): the filter's order x 6, as in Pro-Q; a bell ignores it

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

    setDesign() may be called on the audio thread at any time: the filters
    keep their state, so a change stays stable. For a value that moves
    (a slider, a pedal), glideTo() goes further: it reaches the new design
    over the next numSamples by moving each section's natural parameters
    (g, k and the output mix) a little every sample, so redesigning every 16
    samples sounds like redesigning every sample (no zipper noise). A section
    that appears or goes away glides from or to a pass-through. Audio thread:
    no allocation, no locks.
*/
template <int MaxChannels = 2>
class FilterBand
{
public:
    void setDesign (const FilterBandDesign& newDesign) noexcept
    {
        design = newDesign;
        glideRemaining = 0;
        activeSections = design.numSections;

        for (int s = 0; s < design.numSections; ++s)
            coefficients[(size_t) s] = SvfCoefficients::fromDesign (design.sections[(size_t) s]);
    }

    /** Moves to newDesign over the next numSamples processed (see above). */
    void glideTo (const FilterBandDesign& newDesign, int numSamples) noexcept
    {
        if (numSamples <= 1)
        {
            setDesign (newDesign);
            return;
        }

        const auto from = activeSections;
        const auto to = newDesign.numSections;
        const auto sections = std::max (from, to);
        std::array<SvfCoefficients, (size_t) FilterBandDesign::maxSections> targets {};

        for (int s = 0; s < sections; ++s)
        {
            const auto i = (size_t) s;
            targets[i] = s < to ? SvfCoefficients::fromDesign (newDesign.sections[i]) : coefficients[i].asPassThrough();

            if (s >= from)
            {
                // A section that wasn't playing starts as a silent pass-through.
                coefficients[i] = targets[i].asPassThrough();
                filters[i].reset();
            }

            const auto n = (double) numSamples;
            steps[i] = { (targets[i].g - coefficients[i].g) / n, (targets[i].k - coefficients[i].k) / n,
                         (targets[i].m0 - coefficients[i].m0) / n, (targets[i].m1 - coefficients[i].m1) / n,
                         (targets[i].m2 - coefficients[i].m2) / n };
        }

        design = newDesign;
        finalCoefficients = targets;
        activeSections = sections;
        glideRemaining = numSamples;
    }

    bool isGliding() const noexcept { return glideRemaining > 0; }

    const FilterBandDesign& getDesign() const noexcept { return design; }

    void reset() noexcept
    {
        for (auto& f : filters)
            f.reset();
    }

    /** One sample of one channel. While gliding, call advanceGlide() once per
        sample (process() does this for you). */
    float processSample (float x, int channel) noexcept
    {
        auto y = (double) x;

        for (int s = 0; s < activeSections; ++s)
            y = filters[(size_t) s].tick (y, channel, coefficients[(size_t) s]);

        return (float) y;
    }

    void advanceGlide() noexcept
    {
        if (glideRemaining <= 0)
            return;

        if (--glideRemaining == 0)
        {
            // Land exactly on the design, and drop sections that went away.
            for (int s = 0; s < activeSections; ++s)
                coefficients[(size_t) s] = finalCoefficients[(size_t) s];

            activeSections = design.numSections;
            return;
        }

        for (int s = 0; s < activeSections; ++s)
        {
            auto& c = coefficients[(size_t) s];
            const auto& d = steps[(size_t) s];
            c.g += d[0];
            c.k += d[1];
            c.m0 += d[2];
            c.m1 += d[3];
            c.m2 += d[4];
            c.update();
        }
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (activeSections == 0)
            return;   // bit-exact passthrough

        if (glideRemaining <= 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    channels[ch][i] = processSample (channels[ch][i], ch);

            return;
        }

        // Gliding: every channel shares each sample's coefficients.
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] = processSample (channels[ch][i], ch);

            advanceGlide();

            if (activeSections == 0)
                return;   // glided to nothing: the rest passes bit-exact
        }
    }

private:
    FilterBandDesign design;
    int activeSections = 0;
    std::array<SvfCoefficients, (size_t) FilterBandDesign::maxSections> coefficients {}, finalCoefficients {};
    std::array<std::array<double, 5>, (size_t) FilterBandDesign::maxSections> steps {};
    int glideRemaining = 0;
    std::array<Svf<MaxChannels>, (size_t) FilterBandDesign::maxSections> filters {};
};

} // namespace encore::dsp
