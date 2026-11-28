// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/SpectralDetector.h"

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

void SpectralDetector::prepare (int maxSize)
{
    maxN = std::max (16, maxSize);
    fft.prepare (maxN);
    ring.assign ((size_t) (2 * maxN), 0.0f);
    levels.assign ((size_t) (2 * (maxN / 2 + 1)), 0.0f);
    work.assign ((size_t) maxN, {});
    window.resize ((size_t) maxN);

    for (int i = 0; i < maxN; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / maxN));

    configure (std::min (1024, maxN), 256, 1);
    reset();
}

void SpectralDetector::configure (int size, int hopSamples, int numChannels) noexcept
{
    n = std::clamp (size, 16, maxN);
    hop = std::max (1, hopSamples);
    channels = std::clamp (numChannels, 1, 2);
    unitsPerSample = 1.25 * (double) (Fft::unitsFor (n) + n) / (double) hop;   // the transform within the hop, with room
}

void SpectralDetector::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (levels.begin(), levels.end(), 0.0f);
    write = 0;
    sinceHop = 0;
    fed = 0;
    running = ready = false;
    credit = 0.0;
}

void SpectralDetector::startTransform() noexcept
{
    // The last N samples, windowed: channel 1 real, channel 2 imaginary.
    const auto stride = maxN / n;

    for (int i = 0; i < n; ++i)
    {
        const auto at = (write - n + i + maxN) % maxN;
        const auto w = window[(size_t) (i * stride)];
        work[(size_t) i] = { ring[(size_t) at] * w, channels > 1 ? ring[(size_t) (maxN + at)] * w : 0.0f };
    }

    fft.start (job, work.data(), n, false);
    running = true;
}

void SpectralDetector::finishTransform() noexcept
{
    // Unpack the two real spectra; a sine of amplitude A peaks at A N / 4 under Hann.
    const auto bins = n / 2 + 1;
    const auto scale = 8.0f / ((float) n * (float) n);   // (2 / (N/2))^2 / 2: power = RMS^2
    auto* l = levels.data();
    auto* r = levels.data() + (maxN / 2 + 1);

    for (int k = 0; k < bins; ++k)
    {
        const auto z = work[(size_t) k];
        const auto zc = std::conj (work[(size_t) ((n - k) % n)]);

        if (channels > 1)
        {
            const auto a = 0.5f * (z + zc);
            const auto bb = (z - zc) * Fft::Complex (0.0f, -0.5f);
            l[k] = std::norm (a) * scale;
            r[k] = std::norm (bb) * scale;
        }
        else
        {
            l[k] = std::norm (z) * scale;
        }
    }

    running = false;
    ready = true;
}

void SpectralDetector::push (const float* a, const float* b, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) write] = a[i];

        if (channels > 1 && b != nullptr)
            ring[(size_t) (maxN + write)] = b[i];

        write = (write + 1) % maxN;
        ++fed;

        if (++sinceHop >= hop)
        {
            sinceHop = 0;

            if (running)
            {
                // The last one didn't finish within its hop (it can't, with the
                // budget above, unless the hop changed): finish it now.
                while (! job.done)
                    fft.step (job, 1 << 30);

                finishTransform();
            }

            if (fed >= n)
                startTransform();
        }
    }

    if (running)
    {
        credit += unitsPerSample * numSamples;
        const auto budget = (int) credit;
        credit -= budget;
        fft.step (job, std::max (1, budget));

        if (job.done)
            finishTransform();
    }
}

} // namespace encore::dsp
