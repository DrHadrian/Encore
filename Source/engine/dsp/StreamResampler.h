// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <vector>

namespace encore::dsp
{

/** Converts a continuous mono stream from one sample rate to another.

    Windowed-sinc interpolation (32 taps, 512 precomputed phases), with the
    cutoff lowered automatically when converting down so nothing aliases. Feed
    it any number of samples per call; it returns however many output samples
    became available. Latency is fixed (half the filter, in input samples).

    prepare() allocates; process()/reset() never do.
*/
class StreamResampler
{
public:
    static constexpr int numTaps = 32;

    void prepare (double inputRate, double outputRate, int maxInputBlock);
    void reset() noexcept;

    /** Consumes all numInput samples; writes up to maxOutput samples and
        returns how many it wrote. maxOutput >= getMaxOutputFor (numInput)
        guarantees nothing is dropped. */
    int process (const float* input, int numInput, float* output, int maxOutput) noexcept;

    int getMaxOutputFor (int numInput) const noexcept;

    /** Delay through this stage, in input samples. */
    double getLatencyInputSamples() const noexcept { return numTaps / 2 - 1; }

private:
    static constexpr int numPhases = 512;

    std::vector<float> kernels;   // (numPhases + 1) x numTaps
    std::vector<float> history;   // last inputs, oldest first
    int historyCount = 0;
    double position = 0.0;        // next output time, in history indices
    double step = 1.0;            // input samples per output sample
};

/** Runs a mono processor at its own sample rate inside a stream at another.

    device rate -> resample -> processor -> resample back -> device rate.
    Every call returns exactly as many samples as it was given, delayed by a
    fixed, reported latency (well under 1 ms), so it slots into a chain like
    any other block. Used for NAM models trained at a rate other than the
    device's.
*/
class RateBridge
{
public:
    void prepare (double deviceRate, double innerRate, int maxDeviceBlock);
    void reset() noexcept;

    /** The largest block the inner processor will be asked to handle. */
    int getMaxInnerBlock() const noexcept { return maxInnerBlock; }

    /** Total delay in device samples (rounded). */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** Times the output ran dry (would be a dropout; should stay 0). */
    int getUnderrunCount() const noexcept { return underruns; }

    /** In place: io holds numSamples device-rate samples in and out.
        innerProcess (float* samples, int count) runs at the inner rate, in place. */
    template <typename InnerProcess>
    void process (float* io, int numSamples, InnerProcess&& innerProcess) noexcept
    {
        const auto innerCount = up.process (io, numSamples, inner.data(), maxInnerBlock);
        innerProcess (inner.data(), innerCount);

        const auto produced = down.process (inner.data(), innerCount, back.data(), (int) back.size());
        pushOutput (back.data(), produced);
        popOutput (io, numSamples);
    }

private:
    void pushOutput (const float* samples, int count) noexcept;
    void popOutput (float* destination, int count) noexcept;

    StreamResampler up, down;
    std::vector<float> inner, back, fifo;
    int fifoRead = 0, fifoCount = 0;
    int maxInnerBlock = 0;
    int primeSamples = 0;
    int latencySamples = 0;
    int underruns = 0;
};

} // namespace encore::dsp
