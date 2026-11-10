// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

namespace
{
    constexpr float threshold = 0.15f;      // YIN's absolute threshold: lower = stricter
    constexpr float fallbackLimit = 0.35f;  // accept the best dip if nothing beat the threshold
}

int PitchDetector::getRequiredSamples (double sampleRate) const noexcept
{
    const auto maxLag = (int) std::ceil (sampleRate / minFrequency);
    return maxLag + 1024;   // the lags searched plus an ~85 ms window at 12 kHz
}

PitchDetector::Result PitchDetector::detect (const float* x, int numSamples, double sampleRate)
{
    const auto maxLag = std::min ((int) std::ceil (sampleRate / minFrequency), numSamples / 2);
    const auto minLag = std::max (2, (int) std::floor (sampleRate / maxFrequency));
    const auto window = numSamples - maxLag;

    if (maxLag <= minLag + 2 || window <= 0)
        return {};

    difference.assign ((size_t) maxLag + 1, 0.0f);

    // Squared difference between the signal and itself delayed by each lag.
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;

        for (int i = 0; i < window; ++i)
        {
            const auto d = (double) x[i] - (double) x[i + lag];
            sum += d * d;
        }

        difference[(size_t) lag] = (float) sum;
    }

    // Cumulative mean normalisation: d'(lag) = d(lag) / mean(d(1..lag)),
    // which removes the bias towards tiny lags and makes 1.0 "unrelated".
    difference[0] = 1.0f;
    double running = 0.0;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += difference[(size_t) lag];
        difference[(size_t) lag] = running > 0.0 ? (float) (difference[(size_t) lag] * lag / running) : 1.0f;
    }

    // The first dip under the threshold (then its bottom) is the period;
    // taking the first, not the deepest, avoids locking onto 2x the period.
    int best = -1;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (difference[(size_t) lag] < threshold)
        {
            while (lag + 1 <= maxLag && difference[(size_t) lag + 1] < difference[(size_t) lag])
                ++lag;

            best = lag;
            break;
        }
    }

    if (best < 0)
    {
        const auto first = difference.begin() + minLag;
        const auto lowest = std::min_element (first, difference.end());

        if (*lowest > fallbackLimit)
            return {};

        best = (int) (lowest - difference.begin());
    }

    // Parabolic interpolation between neighbouring lags: sub-sample period,
    // so the reading is accurate to well under a cent.
    auto period = (float) best;

    if (best > 1 && best < maxLag)
    {
        const auto a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
        const auto denominator = a - 2.0f * b + c;

        if (std::abs (denominator) > 1.0e-9f)
            period += 0.5f * (a - c) / denominator;
    }

    if (period <= 0.0f)
        return {};

    // High notes have short periods, so a fraction of a sample is many cents.
    // Measure k periods at once instead (the dip near k x period), which
    // divides the error by k.
    const auto multiple = (int) ((float) maxLag * 0.9f / period);

    if (multiple >= 2)
    {
        auto lag = (int) std::lround (period * (float) multiple);

        for (int step = 0; step < 3; ++step)   // slide to the bottom of that dip
        {
            if (lag + 1 < maxLag && difference[(size_t) lag + 1] < difference[(size_t) lag])
                ++lag;
            else if (lag - 1 > minLag && difference[(size_t) lag - 1] < difference[(size_t) lag])
                --lag;
        }

        if (lag > 1 && lag < maxLag)
        {
            const auto a = difference[(size_t) lag - 1], b = difference[(size_t) lag], c = difference[(size_t) lag + 1];
            const auto denominator = a - 2.0f * b + c;
            auto refined = (float) lag;

            if (std::abs (denominator) > 1.0e-9f)
                refined += 0.5f * (a - c) / denominator;

            // Only if it really is the same pitch (not a neighbouring dip).
            if (std::abs (refined / (float) multiple - period) < 0.5f)
                period = refined / (float) multiple;
        }
    }

    return { (float) (sampleRate / period), std::clamp (1.0f - difference[(size_t) best], 0.0f, 1.0f) };
}

} // namespace encore::dsp
