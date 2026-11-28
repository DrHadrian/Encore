// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <complex>
#include <vector>

namespace encore::dsp
{

/** An in-place radix-2 complex FFT (float), for the Encore Engine's
    linear-phase and spectral work (B5, decision 0069). Our own code: an
    iterative Cooley-Tukey with a precomputed bit-reversal table and
    twiddles, so it never allocates once prepared.

    It can also be run **in steps** (FftJob): the same transform, a bounded
    number of butterflies at a time, so a large FFT can be spread evenly over
    several audio callbacks instead of landing in one of them.

    prepare(): message thread (allocates). Everything else: any thread, no
    allocation, for sizes up to the prepared one.
*/
class Fft
{
public:
    using Complex = std::complex<float>;

    /** Ready for every power-of-two size up to maxSize. */
    void prepare (int maxSize);
    int getMaxSize() const noexcept { return maxSize; }

    /** Forward (e^-j) or inverse (e^+j, not scaled) transform of size n (a
        power of two, at most the prepared size), in place. */
    void perform (Complex* data, int n, bool inverse) const noexcept;

    /** A transform done in steps; start(), then step() until it returns true. */
    struct Job
    {
        Complex* data = nullptr;
        int size = 0;
        bool inverse = false;
        int phase = 0;          // 0: bit reversal, then one per stage
        int position = 0;       // within the phase
        bool done = true;
    };

    void start (Job& job, Complex* data, int n, bool inverse) const noexcept;

    /** Does at most `budget` units of work (a butterfly, or a swap of the bit
        reversal); returns how many it used. job.done once finished. */
    int step (Job& job, int budget) const noexcept;

    /** Units of work a transform of size n takes in steps. */
    static int unitsFor (int n) noexcept;

private:
    int maxSize = 0;
    int maxBits = 0;
    std::vector<int> bitReverse;      // for maxSize; smaller sizes shift
    std::vector<Complex> twiddles;    // e^(-j 2 pi k / maxSize), k < maxSize / 2
};

} // namespace encore::dsp
