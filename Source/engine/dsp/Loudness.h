// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/dsp/FilterBand.h"

#include <array>
#include <cmath>

namespace encore::dsp
{

/** K-weighting (ITU-R BS.1770): the "head" high shelf (+4 dB above about
    1.7 kHz) and the RLB high pass (about 38 Hz), designed for any sample
    rate from the standard's analog parameters (our own code: bilinear, as
    the standard's 48 kHz coefficients are). Plain maths, any thread. */
inline FilterBandDesign kWeighting (double sampleRate) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    FilterBandDesign d;
    d.numSections = 2;

    {
        // Stage 1: the shelf. f0 1681.97 Hz, gain 4.0 dB, Q 0.7072.
        const auto f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const auto k = std::tan (pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        auto& s = d.sections[0];
        s.b0 = (vh + vb * k / q + k * k) / a0;
        s.b1 = 2.0 * (k * k - vh) / a0;
        s.b2 = (vh - vb * k / q + k * k) / a0;
        s.a1 = 2.0 * (k * k - 1.0) / a0;
        s.a2 = (1.0 - k / q + k * k) / a0;
    }

    {
        // Stage 2: the high pass. f0 38.14 Hz, Q 0.5003; its numerator 1, -2, 1.
        const auto f0 = 38.13547087602444, q = 0.5003270373238773;
        const auto k = std::tan (pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;
        auto& s = d.sections[1];
        s.b0 = 1.0;
        s.b1 = -2.0;
        s.b2 = 1.0;
        s.a1 = 2.0 * (k * k - 1.0) / a0;
        s.a2 = (1.0 - k / q + k * k) / a0;
    }

    return d;
}

/** Loudness over a sliding window (BS.1770 K-weighted mean square summed over
    the channels, in LUFS), in bins (Proposed: 100 ms bins, a 3 s window).

    Threads: prepare() off the audio thread; process() and the getters on the
    audio thread, no allocation. */
class SlidingLoudness
{
public:
    static constexpr int maxBins = 64;
    static constexpr double silenceLufs = -200.0;

    void prepare (double sampleRate, double windowSeconds = 3.0, double binSeconds = 0.1) noexcept
    {
        filter.setDesign (kWeighting (sampleRate));
        binSamples = std::max (1, (int) std::lround (binSeconds * sampleRate));
        numBins = std::clamp ((int) std::lround (windowSeconds / binSeconds), 1, maxBins);
        reset();
    }

    void reset() noexcept
    {
        filter.reset();
        bins.fill (0.0);
        binSum = 0.0;
        lastBin = 0.0;
        inBin = 0;
        binIndex = 0;
        binsFilled = 0;
        windowSum = 0.0;
        newBin = false;
    }

    /** Measures without changing the audio. Returns true if a bin closed in
        this call (the loudness moved on). */
    bool process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        newBin = false;
        numChannels = std::clamp (numChannels, 1, 2);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto y = (double) filter.processSample (channels[ch][i], ch);
                binSum += y * y;
            }

            if (++inBin >= binSamples)
                closeBin();
        }

        return newBin;
    }

    /** The last closed bin's loudness, LUFS: what is playing right now. */
    double getLastBinLufs() const noexcept
    {
        return lastBin > 0.0 ? -0.691 + 10.0 * std::log10 (lastBin / binSamples) : silenceLufs;
    }

    /** The window's loudness, LUFS (silenceLufs before anything was heard). */
    double getLufs() const noexcept
    {
        if (binsFilled == 0 || windowSum <= 0.0)
            return silenceLufs;

        return -0.691 + 10.0 * std::log10 (windowSum / ((double) binsFilled * binSamples));
    }

private:
    void closeBin() noexcept
    {
        windowSum -= bins[(size_t) binIndex];
        bins[(size_t) binIndex] = binSum;
        lastBin = binSum;
        windowSum = std::max (0.0, windowSum + binSum);
        binIndex = (binIndex + 1) % numBins;
        binsFilled = std::min (binsFilled + 1, numBins);
        binSum = 0.0;
        inBin = 0;
        newBin = true;
    }

    FilterBand<2> filter;
    std::array<double, maxBins> bins {};
    double binSum = 0.0, windowSum = 0.0, lastBin = 0.0;
    int binSamples = 4800, numBins = 30, inBin = 0, binIndex = 0, binsFilled = 0;
    bool newBin = false;
};

} // namespace encore::dsp
