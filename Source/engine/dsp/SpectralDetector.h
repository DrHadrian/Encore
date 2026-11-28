// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/dsp/Fft.h"

#include <vector>

namespace encore::dsp
{

/** The level of each bin of a signal, for spectral dynamics (B5, decision
    0069): a Hann-windowed FFT of the last N samples every `hop` samples,
    one or two channels (two are packed into one complex FFT). Levels are
    power per bin, scaled so a steady sine of RMS r reads r^2 in its bin.

    The FFT is spread over the samples of the hop (Fft::Job), so pushing a
    64-sample buffer does a fixed share of it: no callback takes the whole
    transform. A new set of levels is ready once per hop (takeNewLevels()).

    prepare(): message thread. Everything else: audio thread, no allocation.
*/
class SpectralDetector
{
public:
    void prepare (int maxSize);
    void configure (int size, int hopSamples, int numChannels) noexcept;   // then reset()
    void reset() noexcept;

    /** Feeds samples (b may be null for one channel). */
    void push (const float* a, const float* b, int numSamples) noexcept;

    /** True once after each new set of levels. */
    bool takeNewLevels() noexcept
    {
        const auto fresh = ready;
        ready = false;
        return fresh;
    }

    const float* getLevels (int channel) const noexcept { return levels.data() + (size_t) channel * (size_t) (maxN / 2 + 1); }
    int getSize() const noexcept                        { return n; }
    int getNumChannels() const noexcept                 { return channels; }

private:
    void startTransform() noexcept;
    void finishTransform() noexcept;

    Fft fft;
    int maxN = 0, n = 1024, hop = 256, channels = 1;
    std::vector<float> ring, window, levels;   // ring: 2 x maxN; levels: 2 x (maxN/2 + 1)
    std::vector<Fft::Complex> work;            // maxN
    int write = 0, sinceHop = 0;
    long long fed = 0;
    Fft::Job job;
    bool running = false, ready = false;
    double unitsPerSample = 1.0, credit = 0.0;
};

} // namespace encore::dsp
