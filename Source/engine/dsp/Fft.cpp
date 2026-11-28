// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace encore::dsp
{

namespace
{
    // Plain complex multiply (std::complex's operator* checks for NaN and infinity: slow).
    inline Fft::Complex mul (Fft::Complex a, Fft::Complex b) noexcept
    {
        return { a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real() };
    }

    int log2Of (int n) noexcept
    {
        int bits = 0;

        while ((1 << bits) < n)
            ++bits;

        return bits;
    }
}

void Fft::prepare (int size)
{
    maxBits = log2Of (std::max (2, size));
    maxSize = 1 << maxBits;
    bitReverse.assign ((size_t) maxSize, 0);

    for (int i = 0; i < maxSize; ++i)
    {
        int r = 0;

        for (int b = 0; b < maxBits; ++b)
            if ((i >> b) & 1)
                r |= 1 << (maxBits - 1 - b);

        bitReverse[(size_t) i] = r;
    }

    twiddles.resize ((size_t) (maxSize / 2));

    for (int k = 0; k < maxSize / 2; ++k)
    {
        const auto a = -2.0 * 3.14159265358979323846 * k / maxSize;
        twiddles[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
    }
}

int Fft::unitsFor (int n) noexcept
{
    return n / 2 + (n / 2) * log2Of (n);   // the swaps (at most n / 2), then n / 2 butterflies per stage
}

void Fft::perform (Complex* x, int n, bool inverse) const noexcept
{
    n = std::clamp (n, 1, maxSize);

    if (n < 2)
        return;

    const auto shift = maxBits - log2Of (n);

    for (int i = 0; i < n; ++i)
    {
        const auto r = bitReverse[(size_t) i] >> shift;

        if (r > i)
            std::swap (x[i], x[r]);
    }

    for (int length = 2; length <= n; length <<= 1)
    {
        const auto half = length / 2;
        const auto stride = maxSize / length;

        for (int block = 0; block < n; block += length)
        {
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddles[(size_t) (k * stride)];

                if (inverse)
                    w = std::conj (w);

                const auto i = block + k, j = i + half;
                const auto t = mul (w, x[j]);
                x[j] = x[i] - t;
                x[i] += t;
            }
        }
    }
}

void Fft::start (Job& job, Complex* data, int n, bool inverse) const noexcept
{
    job.data = data;
    job.size = std::clamp (n, 1, maxSize);
    job.inverse = inverse;
    job.phase = 0;
    job.position = 0;
    job.done = job.size < 2 || maxSize == 0;
}

int Fft::step (Job& job, int budget) const noexcept
{
    if (job.done)
        return 0;

    const auto n = job.size;
    const auto bits = log2Of (n);
    const auto shift = maxBits - bits;
    auto* x = job.data;
    int used = 0;

    while (used < budget && ! job.done)
    {
        if (job.phase == 0)
        {
            // Bit reversal: n positions, a swap where the partner is higher.
            const auto end = std::min (n, job.position + std::max (1, 2 * (budget - used)));

            for (int i = job.position; i < end; ++i)
            {
                const auto r = bitReverse[(size_t) i] >> shift;

                if (r > i)
                    std::swap (x[i], x[r]);
            }

            used += (end - job.position + 1) / 2;
            job.position = end;

            if (job.position >= n)
            {
                job.phase = 1;
                job.position = 0;
            }

            continue;
        }

        // Stage s (1..bits): blocks of `length`, half `half`; butterflies are
        // numbered 0 .. n/2 - 1 across the stage.
        const auto length = 1 << job.phase;
        const auto half = length / 2;
        const auto stride = maxSize / length;   // twiddle step
        const auto end = std::min (n / 2, job.position + (budget - used));
        auto block = (job.position / half) * length;
        auto k = job.position % half;

        for (int b = job.position; b < end; ++b)
        {
            auto w = twiddles[(size_t) (k * stride)];

            if (job.inverse)
                w = std::conj (w);

            const auto i = block + k, j = i + half;
            const auto t = mul (w, x[j]);
            x[j] = x[i] - t;
            x[i] += t;

            if (++k == half)
            {
                k = 0;
                block += length;
            }
        }

        used += end - job.position;
        job.position = end;

        if (job.position >= n / 2)
        {
            job.position = 0;

            if (++job.phase > bits)
                job.done = true;
        }
    }

    return used;
}

} // namespace encore::dsp
