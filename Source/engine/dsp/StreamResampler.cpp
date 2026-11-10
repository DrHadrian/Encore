// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/StreamResampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace encore::dsp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double kaiserBeta = 8.6;    // ~-90 dB sidelobes
    constexpr double passband = 0.92;     // fraction of the lower Nyquist kept

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < sum * 1.0e-12)
                break;
        }

        return sum;
    }
}

//==============================================================================
void StreamResampler::prepare (double inputRate, double outputRate, int maxInputBlock)
{
    assert (inputRate > 0.0 && outputRate > 0.0 && maxInputBlock > 0);

    step = inputRate / outputRate;

    // Cutoff relative to the input's Nyquist: lowered when going down in rate.
    const auto cutoff = passband * std::min (1.0, outputRate / inputRate);
    const int half = numTaps / 2;
    const auto i0Beta = besselI0 (kaiserBeta);

    kernels.assign ((size_t) (numPhases + 1) * numTaps, 0.0f);

    for (int phase = 0; phase <= numPhases; ++phase)
    {
        const auto fraction = (double) phase / numPhases;
        double sum = 0.0;
        std::vector<double> taps ((size_t) numTaps);

        for (int k = 0; k < numTaps; ++k)
        {
            const auto d = (k - (half - 1)) - fraction;       // distance from the output instant
            const auto x = cutoff * d;
            const auto sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const auto r = d / half;
            const auto window = std::abs (r) >= 1.0 ? 0.0 : besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / i0Beta;
            taps[(size_t) k] = cutoff * sinc * window;
            sum += taps[(size_t) k];
        }

        for (int k = 0; k < numTaps; ++k)   // unity gain at DC for every phase
            kernels[(size_t) (phase * numTaps + k)] = (float) (taps[(size_t) k] / sum);
    }

    history.assign ((size_t) (numTaps + 2 + maxInputBlock), 0.0f);
    reset();
}

void StreamResampler::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);

    // Start with half a filter of silence so output can begin right away.
    historyCount = numTaps / 2;
    position = numTaps / 2 - 1;
}

int StreamResampler::getMaxOutputFor (int numInput) const noexcept
{
    return (int) std::ceil (numInput / step) + 2;
}

int StreamResampler::process (const float* input, int numInput, float* output, int maxOutput) noexcept
{
    constexpr int half = numTaps / 2;
    int produced = 0;

    // Larger-than-prepared inputs are taken in pieces that fit the history.
    const auto capacity = (int) history.size() - (numTaps + 2);

    for (int offset = 0; offset < numInput;)
    {
        const auto chunk = std::min (capacity, numInput - offset);
        std::memcpy (history.data() + historyCount, input + offset, sizeof (float) * (size_t) chunk);
        historyCount += chunk;
        offset += chunk;

        while (produced < maxOutput)
        {
            const auto index = (int) position;

            if (index + half >= historyCount)
                break;

            const auto phase = (position - index) * numPhases;
            const auto p0 = (int) phase;
            const auto t = (float) (phase - p0);
            const float* k0 = kernels.data() + p0 * numTaps;
            const float* k1 = k0 + numTaps;
            const float* x = history.data() + index - (half - 1);

            float a = 0.0f, b = 0.0f;

            for (int k = 0; k < numTaps; ++k)
            {
                a += x[k] * k0[k];
                b += x[k] * k1[k];
            }

            output[produced++] = a + (b - a) * t;
            position += step;
        }

        // Drop history the next output no longer needs.
        const auto keepFrom = std::max (0, (int) position - (half - 1));

        if (keepFrom > 0)
        {
            const auto remaining = std::max (0, historyCount - keepFrom);
            std::memmove (history.data(), history.data() + keepFrom, sizeof (float) * (size_t) remaining);
            historyCount = remaining;
            position -= keepFrom;
        }
    }

    return produced;
}

//==============================================================================
void RateBridge::prepare (double deviceRate, double innerRate, int maxDeviceBlock)
{
    up.prepare (deviceRate, innerRate, maxDeviceBlock);
    maxInnerBlock = up.getMaxOutputFor (maxDeviceBlock);
    down.prepare (innerRate, deviceRate, maxInnerBlock);

    inner.assign ((size_t) maxInnerBlock, 0.0f);
    back.assign ((size_t) down.getMaxOutputFor (maxInnerBlock), 0.0f);

    // Each stage holds back about half a filter (its look-ahead). Pre-filling
    // the output with that much silence, plus a little margin for per-call
    // rounding, means a full block is always ready. That amount is the latency.
    const auto filterDelay = (StreamResampler::numTaps / 2) * (1.0 + deviceRate / innerRate);
    primeSamples = (int) std::ceil (filterDelay) + 4;

    fifo.assign ((size_t) (2 * maxDeviceBlock + 2 * primeSamples + 64), 0.0f);

    // The exact delay depends on the rate pair and filter phase, so measure it:
    // send an impulse through with nothing inside, find where it comes out.
    reset();
    std::vector<float> probe ((size_t) (4 * primeSamples + maxDeviceBlock), 0.0f);
    probe[0] = 1.0f;

    for (int offset = 0; offset < (int) probe.size(); offset += maxDeviceBlock)
        process (probe.data() + offset, std::min (maxDeviceBlock, (int) probe.size() - offset), [] (float*, int) {});

    latencySamples = (int) std::distance (probe.begin(),
                                          std::max_element (probe.begin(), probe.end(),
                                                            [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    reset();
}

void RateBridge::reset() noexcept
{
    up.reset();
    down.reset();
    std::fill (fifo.begin(), fifo.end(), 0.0f);
    fifoRead = 0;
    fifoCount = primeSamples;
    underruns = 0;
}

void RateBridge::pushOutput (const float* samples, int count) noexcept
{
    const auto size = (int) fifo.size();

    for (int i = 0; i < count && fifoCount < size; ++i)
    {
        fifo[(size_t) ((fifoRead + fifoCount) % size)] = samples[i];
        ++fifoCount;
    }
}

void RateBridge::popOutput (float* destination, int count) noexcept
{
    const auto size = (int) fifo.size();
    const auto available = std::min (count, fifoCount);

    for (int i = 0; i < available; ++i)
    {
        destination[i] = fifo[(size_t) fifoRead];
        fifoRead = (fifoRead + 1) % size;
    }

    fifoCount -= available;

    if (available < count)
    {
        std::fill (destination + available, destination + count, 0.0f);
        ++underruns;
    }
}

} // namespace encore::dsp
