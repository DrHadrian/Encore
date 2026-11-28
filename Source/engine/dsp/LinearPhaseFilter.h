// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/dsp/Fft.h"
#include "engine/dsp/FilterBand.h"

#include <array>
#include <cstdint>
#include <vector>

namespace encore::dsp
{

/** The Encore Engine's linear-phase filter (B5, decision 0069): plays the
    zero-phase magnitude of a set of bands as a linear-phase FIR, by exact
    linear convolution, with a kernel that follows the bands frame by frame.

    **What it plays.** The product of the bands' magnitudes (their designs'
    |H|, so gain scale and a dynamic band's movement come with them), sampled
    on N/2 + 1 bins, made into a real, even spectrum, inverse transformed, cut
    to N - 1 taps centred at N/2 under a Kaiser window (beta 8), and played
    by uniformly partitioned overlap-save convolution: partitions of B
    samples, P = N / B of them, a frequency-domain delay line of the input.
    A Brickwall band is the ideal step on the bins; the window makes it fall
    from -1 to -60 dB in about 3.2 bins, with 61-67 dB of rejection.

    **Latency** of a stage, exactly: N / 2 (the kernel's centre) + B (the
    input is gathered in blocks of B). Two stages in series add up.

    **Sizes** (Proposed; the same time at every rate: N and B scale with the
    rate's power of two, 1 at 44.1 and 48 kHz, 2 at 88.2 and 96, 4 at 176.4
    and 192): B = 64; N = 256, 1024, 4096 (Low, Medium, High). At 48 kHz:
    latency 192, 576, 2112 samples (4.0, 12.0, 44.0 ms); bins 187.5, 46.9,
    11.7 Hz apart.

    **Stages** (Pro-Q's way): the L/R stage has a kernel per channel (Both,
    Left, Right bands), the M/S stage a kernel for M and one for S (Mid, Side
    bands), encoded M = (L + R) / 2, S = (L - R) / 2. With both in use they
    run one after the other (L/R first) and the latency doubles. On mono, one
    kernel. The configuration is set with configure() and takes effect with
    reset(): the owner runs a second instance to change it while playing.

    **Following the bands.** The owner edits a slot (its designs, target,
    Brickwall, optional per-bin gains) and calls slotChanged(). Whenever
    something has changed, a new kernel is designed once per frame and
    crossfaded from the old one over the next frame (both kernels play
    during the fade, sample-exact linear crossfade): no step, no click.
    Nothing changed: no design, one kernel. The design runs in the audio
    callback, spread evenly: every block of B samples does a fixed budget of
    work (slot magnitudes, the inverse FFT in steps, the window, the
    partition FFTs), so no callback takes a spike. A frame is Low 2, Medium
    4, High 16 blocks (2.7, 5.3, 21.3 ms at 48 kHz) with two kernels and a
    few moving bands to design; more work makes a frame longer, never a
    callback heavier.

    Threads: prepare() on the message thread (allocates everything, for High
    at this rate, both stages, two channels). Everything else on the audio
    thread, no allocation, no locks; kernelResponseDb() for tests.
*/
class LinearPhaseFilter
{
public:
    enum class Resolution { low, medium, high };
    enum class Stages { lr, ms, both };
    enum class Target { both, left, right, mid, side };   // both: a Stereo band (and every band on mono)
    enum class Brickwall { none, lowCut, highCut };

    static constexpr int maxSlots = 24;
    static constexpr int maxDesignsPerSlot = 17;   // a band and up to 16 spectral sub-bands
    static constexpr double kaiserBeta = 8.0;
    static constexpr int basePartition = 64;
    static constexpr std::array<int, 3> baseLength { 256, 1024, 4096 };
    static constexpr std::array<int, 3> frameBlocks { 2, 4, 16 };

    struct Slot
    {
        bool active = false;
        Target target = Target::both;
        std::array<FilterBandDesign, (size_t) maxDesignsPerSlot> designs {};
        int numDesigns = 0;
        Brickwall brickwall = Brickwall::none;   // plays instead of the designs
        double brickwallFrequency = 1000.0;
        bool binGains = false;                   // also apply getBinGains (slot), dB per bin
    };

    /** Message thread: everything for High at this rate, both stages, two channels. */
    void prepare (double sampleRate, int maxChannels = 2);

    /** The resolution, stages and channels to play from the next reset().
        Audio thread or before playing; no allocation. */
    void configure (Resolution resolution, Stages stages, int numChannels) noexcept;

    /** Starts afresh in the configuration: silence in the delay lines, the
        identity kernel, every active slot to be designed. Audio thread OK. */
    void reset() noexcept;

    LinearPhaseFilter();

    Slot& getSlot (int index) noexcept                  { return slots[(size_t) index]; }
    float* getBinGains (int index) noexcept             { return binGainStore.data() + (size_t) index * (size_t) maxBins; }
    void slotChanged (int index) noexcept               { dirty |= (std::uint32_t) 1 << index; }

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept              { return latency; }
    static int latencyFor (Resolution resolution, Stages stages, double sampleRate) noexcept;
    static int rateFactor (double sampleRate) noexcept;

    /** Fed long enough to play the whole kernel, every change designed, no
        fade: what it plays now is what its slots say. */
    bool isSettled() const noexcept;

    Resolution getResolution() const noexcept           { return resolution; }
    Stages getStages() const noexcept                   { return stages; }
    int getKernelLength() const noexcept                { return n; }
    int getPartitionSize() const noexcept               { return b; }
    int getNumBins() const noexcept                     { return n / 2 + 1; }
    double getBinFrequency (int bin) const noexcept     { return bin * rate / n; }
    int getFrameSamples() const noexcept                { return frameBlocks[(size_t) resolution] * b; }
    int getFramesDesigned() const noexcept              { return framesDesigned; }

    /** The design work a block may do (units: a butterfly, or a section at a
        bin), and the most any block has done since reset(): the work per
        callback is bounded by construction, whatever the machine is doing. */
    int getBudgetPerBlock() const noexcept              { return budgetPerBlock; }
    int getMostWorkInABlock() const noexcept            { return mostWork; }

    /** The response of a kernel now playing (0 L, 1 R, 2 M, 3 S) at a
        frequency, dB, from its taps. Tests and diagnostics. */
    double kernelResponseDb (int kernel, double frequency) const noexcept;

private:
    static constexpr int numKernels = 4;   // L, R, M, S
    static constexpr int numSets = 3;      // old, current, next

    struct Kernel
    {
        std::array<std::vector<float>, numSets> re, im;   // P x (B + 1) each
        std::array<std::vector<float>, numSets> taps;     // N each (for kernelResponseDb)
        int current = 0, old = -1;                        // old: fading out (-1: none)
    };

    struct Channel
    {
        std::vector<float> in, out, previous;   // B each
        std::vector<float> fdlRe, fdlIm;        // P x (B + 1), a ring
        int head = 0;
    };

    void processBlockOf (int stage, int ch, int kernel) noexcept;
    void designTick() noexcept;
    void startFrame() noexcept;
    bool designStep (int& budget) noexcept;   // true when the frame's design is complete
    void finishFrame() noexcept;
    int kernelFor (int stage, int ch) const noexcept;
    int freeSet (const Kernel& k) const noexcept;
    void setIdentity (Kernel& k, int set) noexcept;
    bool kernelHas (int kernel, Target target) const noexcept;

    // Sizes.
    double rate = 48000.0;
    int factor = 1, b = 64, n = 1024, p = 16, maxB = 64, maxN = 4096, maxP = 64, maxBins = 2049;
    int numChannels = 2, latency = 0;
    Resolution resolution = Resolution::medium;
    Stages stages = Stages::lr;

    Fft fft;
    std::array<Kernel, numKernels> kernels;
    std::array<std::array<Channel, 2>, 2> channelState;   // [stage][channel]
    std::vector<Fft::Complex> blockScratch, blockOut, blockOld;   // 2B
    std::vector<float> stageBuffer;                               // B x 2: between the stages
    int position = 0;                                             // within the current block (both stages aligned)
    long long fed = 0;

    // Tables per resolution, made in prepare: cos w, cos 2w per bin; the window.
    std::array<std::vector<double>, 3> cos1, cos2;
    std::array<std::vector<float>, 3> windows;

    // Slots and their magnitudes (power, per bin).
    std::vector<Slot> slots, snapshot;                  // maxSlots each (on the heap: they're large)
    std::vector<float> binGainStore, binGainSnapshot;   // maxSlots x maxBins, dB
    std::vector<float> slotPower;                       // maxSlots x maxBins
    std::uint32_t dirty = 0;

    // The design in progress.
    enum class Phase { idle, slots, sum, inverse, window, partitions, waiting };
    Phase phase = Phase::idle;
    std::uint32_t frameSlots = 0;
    std::array<bool, numKernels> designKernel {};
    bool rightFollowsLeft = false;
    int designSlot = 0, designKernelIndex = 0, designPosition = 0;
    std::vector<Fft::Complex> designBuffer;   // N
    Fft::Job inverseJob;
    int budgetPerBlock = 0, mostWork = 0;
    std::array<int, numKernels> nextSet {};
    bool copyRight = false;

    // The crossfade between kernels.
    bool fading = false;
    int fadePosition = 0, fadeLength = 1;
    int framesDesigned = 0;
};

} // namespace encore::dsp
