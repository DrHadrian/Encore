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

    switch (settings.shape)
    {
        case FilterShape::bell:
        case FilterShape::lowShelf:
        case FilterShape::highShelf:
            if (std::abs (gainDb) < 1.0e-6)
                break;   // nothing to do: bit-exact

            add (settings.shape == FilterShape::bell     ? design::matchedBell (sampleRate, f, q, gainDb)
                 : settings.shape == FilterShape::lowShelf ? design::matchedLowShelf (sampleRate, f, q, gainDb)
                                                           : design::matchedHighShelf (sampleRate, f, q, gainDb));
            break;

        case FilterShape::lowCut:
        case FilterShape::highCut:
        {
            // Butterworth of order slope / 6: pairs of poles, plus one first-order section if odd.
            const auto order = std::clamp ((settings.slopeDbPerOctave + 3) / 6, 1, maxSlopeDbPerOctave / 6);
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

        case FilterShape::notch:    add (design::notch (sampleRate, f, q)); break;
        case FilterShape::bandPass: add (design::bandPass (sampleRate, f, q)); break;
        case FilterShape::allPass:  add (design::allPass (sampleRate, f, q)); break;
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
