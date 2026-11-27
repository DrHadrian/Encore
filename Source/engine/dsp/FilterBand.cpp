// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/FilterBand.h"

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

FilterBandDesign FilterBandDesign::make (const FilterSettings& settings, double sampleRate) noexcept
{
    FilterBandDesign d;
    const auto f = settings.frequency;
    const auto q = settings.q;
    const auto gainDb = std::clamp (settings.gainDb, -maxGainDb, maxGainDb);

    auto add = [&d] (const BiquadDesign& section) noexcept { d.sections[(size_t) d.numSections++] = section; };

    // The slope is the filter's order x 6 dB/oct, as on the cuts and in Pro-Q
    // (a 12 dB/oct band pass is second order, so each skirt falls 6 dB/oct).
    const auto order = std::clamp ((settings.slopeDbPerOctave + 3) / 6, 1, maxSlopeDbPerOctave / 6);

    switch (settings.shape)
    {
        case FilterShape::bell:
            if (std::abs (gainDb) < 1.0e-6)
                break;   // nothing to do: bit-exact

            add (design::matchedBell (sampleRate, f, q, gainDb));   // one section: a bell has no slope
            break;

        case FilterShape::lowShelf:
        case FilterShape::highShelf:
        case FilterShape::tiltShelf:
        {
            if (std::abs (gainDb) < 1.0e-6)
                break;

            // A tilt is a high shelf of the gain, the whole of it lowered by half.
            const auto low = settings.shape == FilterShape::lowShelf;
            const auto shelf = [&] (double sectionQ, double sectionDb) noexcept
            {
                return low ? design::matchedLowShelf (sampleRate, f, sectionQ, sectionDb)
                           : design::matchedHighShelf (sampleRate, f, sectionQ, sectionDb);
            };

            if (order == 2)
            {
                add (shelf (q, gainDb));   // 12 dB/oct: Q shapes the corner
            }
            else
            {
                // A Butterworth shelf of the order: pole pairs on the Butterworth
                // circle, each a matched second-order shelf of 2/N of the gain
                // at the same frequency, plus a first-order one if N is odd.
                for (int pair = 0; pair < order / 2; ++pair)
                    add (shelf (design::butterworthQ (order, pair), gainDb * 2.0 / order));

                if (order % 2 != 0)
                    add (low ? design::matchedFirstOrderLowShelf (sampleRate, f, gainDb / order)
                             : design::matchedFirstOrderHighShelf (sampleRate, f, gainDb / order));
            }

            if (settings.shape == FilterShape::tiltShelf)
            {
                const auto half = std::pow (10.0, -gainDb / 40.0);
                auto& first = d.sections[0];
                first.b0 *= half;
                first.b1 *= half;
                first.b2 *= half;
            }

            break;
        }

        case FilterShape::lowCut:
        case FilterShape::highCut:
        {
            // Butterworth of the order: pairs of poles, plus one first-order section if odd.
            const auto lowCut = settings.shape == FilterShape::lowCut;

            for (int pair = 0; pair < order / 2; ++pair)
            {
                const auto sectionQ = design::butterworthQ (order, pair);
                add (lowCut ? design::highPass (sampleRate, f, sectionQ) : design::lowPass (sampleRate, f, sectionQ));
            }

            if (order % 2 != 0)
                add (lowCut ? design::firstOrderHighPass (sampleRate, f) : design::firstOrderLowPass (sampleRate, f));

            break;
        }

        case FilterShape::notch:
        {
            // Second-order notches, one per pair of poles (6 dB/oct plays as 12):
            // steeper is wider at the same Q.
            const auto sections = std::max (1, (order + 1) / 2);

            for (int i = 0; i < sections; ++i)
                add (design::notch (sampleRate, f, q));

            break;
        }

        case FilterShape::bandPass:
        {
            // Second-order band passes centred on the frequency, one per pair of
            // poles (6 dB/oct plays as 12). Each section's Q is raised so that
            // the -3 dB width stays the one Q sets: steeper skirts, same width.
            const auto sections = std::max (1, (order + 1) / 2);
            const auto sectionQ = q * std::sqrt (std::pow (2.0, 1.0 / sections) - 1.0);

            for (int i = 0; i < sections; ++i)
                add (design::bandPass (sampleRate, f, sections == 1 ? q : sectionQ));

            break;
        }

        case FilterShape::allPass:
            // The order turns the phase: 180 degrees at the frequency per pair of poles.
            for (int pair = 0; pair < order / 2; ++pair)
                add (design::allPass (sampleRate, f, q));

            if (order % 2 != 0)
                add (design::firstOrderAllPass (sampleRate, f));

            break;
    }

    return d;
}

double FilterBandDesign::magnitudeDb (double frequency, double sampleRate) const noexcept
{
    auto db = 0.0;

    for (int s = 0; s < numSections; ++s)
        db += sections[(size_t) s].magnitudeDb (frequency, sampleRate);

    return db;
}

double responseDb (std::span<const FilterBandDesign> bands, double frequency, double sampleRate) noexcept
{
    auto db = 0.0;

    for (const auto& band : bands)
        db += band.magnitudeDb (frequency, sampleRate);

    return db;
}

} // namespace encore::dsp
