// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/LinearPhaseFilter.h"

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double stopPower = 1.0e-12;   // a Brickwall's stopband on the bins (-120 dB); the window sets what's heard

    double besselI0 (double x) noexcept
    {
        auto sum = 1.0, term = 1.0;

        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < 1.0e-12 * sum)
                break;
        }

        return sum;
    }

}

LinearPhaseFilter::LinearPhaseFilter() : slots ((size_t) maxSlots), snapshot ((size_t) maxSlots) {}

int LinearPhaseFilter::rateFactor (double sampleRate) noexcept
{
    // The power of two nearest the rate over 48 kHz: 44.1 and 48 -> 1, 88.2 and 96 -> 2, 176.4 and 192 -> 4.
    auto f = 1;

    while (sampleRate / 48000.0 > f * 1.5)
        f *= 2;

    return f;
}

int LinearPhaseFilter::latencyFor (Resolution r, Stages st, double sampleRate) noexcept
{
    const auto f = rateFactor (sampleRate);
    const auto one = baseLength[(size_t) r] * f / 2 + basePartition * f;
    return st == Stages::both ? 2 * one : one;
}

void LinearPhaseFilter::prepare (double sampleRate, int maxChannels)
{
    rate = sampleRate;
    factor = rateFactor (sampleRate);
    maxB = basePartition * factor;
    maxN = baseLength[2] * factor;
    maxP = maxN / maxB;
    maxBins = maxN / 2 + 1;
    (void) maxChannels;

    fft.prepare (maxN);

    for (auto& k : kernels)
    {
        for (int s = 0; s < numSets; ++s)
        {
            k.re[(size_t) s].assign ((size_t) (maxP * (maxB + 1)), 0.0f);
            k.im[(size_t) s].assign ((size_t) (maxP * (maxB + 1)), 0.0f);
            k.taps[(size_t) s].assign ((size_t) maxN, 0.0f);
        }
    }

    for (auto& stage : channelState)
    {
        for (auto& c : stage)
        {
            c.in.assign ((size_t) maxB, 0.0f);
            c.out.assign ((size_t) maxB, 0.0f);
            c.previous.assign ((size_t) maxB, 0.0f);
            c.fdlRe.assign ((size_t) (maxP * (maxB + 1)), 0.0f);
            c.fdlIm.assign ((size_t) (maxP * (maxB + 1)), 0.0f);
        }
    }

    blockScratch.assign ((size_t) (2 * maxB), {});
    blockOut.assign ((size_t) (2 * maxB), {});
    blockOld.assign ((size_t) (2 * maxB), {});
    designBuffer.assign ((size_t) maxN, {});

    for (int r = 0; r < 3; ++r)
    {
        const auto length = baseLength[(size_t) r] * factor;
        const auto bins = length / 2 + 1;
        cos1[(size_t) r].resize ((size_t) bins);
        cos2[(size_t) r].resize ((size_t) bins);

        for (int k = 0; k < bins; ++k)
        {
            const auto w = 2.0 * pi * k / length;
            cos1[(size_t) r][(size_t) k] = std::cos (w);
            cos2[(size_t) r][(size_t) k] = std::cos (2.0 * w);
        }

        // Kaiser over taps 1 .. N-1 (symmetric about N/2); tap 0 is zero.
        auto& w = windows[(size_t) r];
        w.assign ((size_t) length, 0.0f);
        const auto l = length - 1;
        const auto norm = besselI0 (kaiserBeta);

        for (int m = 0; m < l; ++m)
        {
            const auto x = 2.0 * m / (l - 1) - 1.0;
            w[(size_t) (m + 1)] = (float) (besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - x * x))) / norm);
        }
    }

    binGainStore.assign ((size_t) (maxSlots * maxBins), 0.0f);
    binGainSnapshot.assign ((size_t) (maxSlots * maxBins), 0.0f);
    slotPower.assign ((size_t) (maxSlots * maxBins), 1.0f);

    configure (Resolution::medium, Stages::lr, 2);
    reset();
}

void LinearPhaseFilter::configure (Resolution r, Stages st, int channelCount) noexcept
{
    resolution = r;
    numChannels = std::clamp (channelCount, 1, 2);
    stages = numChannels == 1 ? Stages::lr : st;   // mono: one kernel
    n = baseLength[(size_t) r] * factor;
    b = basePartition * factor;
    p = n / b;
    latency = latencyFor (r, stages, rate);
    fadeLength = frameBlocks[(size_t) r] * b;

    // The work per block: a frame designs two kernels and a few moving bands.
    const auto bins = n / 2 + 1;
    const auto kernelUnits = bins + Fft::unitsFor (n) + n / 2 + p * Fft::unitsFor (2 * b);
    budgetPerBlock = (2 * kernelUnits + 8 * bins) / frameBlocks[(size_t) r] + 1;
}

void LinearPhaseFilter::setIdentity (Kernel& k, int set) noexcept
{
    // A delta at N/2: partition (N/2)/B, at its start (N/2 is a multiple of B).
    auto& re = k.re[(size_t) set];
    auto& im = k.im[(size_t) set];
    std::fill (re.begin(), re.begin() + p * (b + 1), 0.0f);
    std::fill (im.begin(), im.begin() + p * (b + 1), 0.0f);
    const auto part = (n / 2) / b;
    std::fill (re.begin() + part * (b + 1), re.begin() + (part + 1) * (b + 1), 1.0f);
    auto& taps = k.taps[(size_t) set];
    std::fill (taps.begin(), taps.begin() + n, 0.0f);
    taps[(size_t) (n / 2)] = 1.0f;
}

void LinearPhaseFilter::reset() noexcept
{
    for (auto& stage : channelState)
    {
        for (auto& c : stage)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f);
            std::fill (c.out.begin(), c.out.end(), 0.0f);
            std::fill (c.previous.begin(), c.previous.end(), 0.0f);
            std::fill (c.fdlRe.begin(), c.fdlRe.end(), 0.0f);
            std::fill (c.fdlIm.begin(), c.fdlIm.end(), 0.0f);
            c.head = 0;
        }
    }

    for (auto& k : kernels)
    {
        setIdentity (k, 0);
        k.current = 0;
        k.old = -1;
    }

    position = 0;
    fed = 0;
    phase = Phase::idle;
    fading = false;
    fadePosition = 0;
    framesDesigned = 0;
    mostWork = 0;
    dirty = 0;

    for (int s = 0; s < maxSlots; ++s)
    {
        std::fill (slotPower.begin() + s * maxBins, slotPower.begin() + (s + 1) * maxBins, 1.0f);

        if (slots[(size_t) s].active)
            dirty |= (std::uint32_t) 1 << s;
    }
}

bool LinearPhaseFilter::isSettled() const noexcept
{
    const auto stageCount = stages == Stages::both ? 2 : 1;
    return fed >= (long long) stageCount * (n + b) && dirty == 0 && phase == Phase::idle && ! fading;
}

bool LinearPhaseFilter::isReady() const noexcept
{
    const auto stageCount = stages == Stages::both ? 2 : 1;
    const auto designed = (dirty == 0 && phase == Phase::idle && ! fading) || framesDesigned >= 2
                          || (framesDesigned >= 1 && ! fading);
    return fed >= (long long) stageCount * (n + b) && designed;
}

int LinearPhaseFilter::kernelFor (int stage, int ch) const noexcept
{
    return stage == 0 ? ch : 2 + ch;
}

int LinearPhaseFilter::freeSet (const Kernel& k) const noexcept
{
    for (int s = 0; s < numSets; ++s)
        if (s != k.current && s != k.old)
            return s;

    return 0;
}

bool LinearPhaseFilter::kernelHas (int kernel, Target target) const noexcept
{
    if (numChannels == 1)
        return kernel == 0;   // mono: every band in the one kernel

    switch (kernel)
    {
        case 0:  return target == Target::both || target == Target::left;
        case 1:  return target == Target::both || target == Target::right;
        case 2:  return target == Target::mid;
        default: return target == Target::side;
    }
}

//==============================================================================
void LinearPhaseFilter::process (float* const* channels, int channelCount, int numSamples) noexcept
{
    const auto nc = std::min (channelCount, numChannels);
    const auto first = stages == Stages::ms ? 1 : 0;
    const auto last = stages == Stages::lr ? 0 : 1;

    for (int start = 0; start < numSamples;)
    {
        const auto m = std::min (b - position, numSamples - start);

        for (int stage = first; stage <= last; ++stage)
        {
            if (stage == 1)
            {
                // Into M/S, around this stage.
                for (int i = start; i < start + m; ++i)
                {
                    const auto l = channels[0][i], r = channels[1][i];
                    channels[0][i] = 0.5f * (l + r);
                    channels[1][i] = 0.5f * (l - r);
                }
            }

            for (int ch = 0; ch < nc; ++ch)
            {
                auto& c = channelState[(size_t) stage][(size_t) ch];

                for (int i = 0; i < m; ++i)
                {
                    c.in[(size_t) (position + i)] = channels[ch][start + i];
                    channels[ch][start + i] = c.out[(size_t) (position + i)];
                }
            }

            if (stage == 1)
            {
                for (int i = start; i < start + m; ++i)
                {
                    const auto mid = channels[0][i], sd = channels[1][i];
                    channels[0][i] = mid + sd;
                    channels[1][i] = mid - sd;
                }
            }
        }

        position += m;
        start += m;

        if (position == b)
        {
            for (int stage = first; stage <= last; ++stage)
                for (int ch = 0; ch < nc; ++ch)
                    processBlockOf (stage, ch, kernelFor (stage, ch));

            if (fading)
            {
                fadePosition += b;

                if (fadePosition >= fadeLength)
                {
                    fading = false;

                    for (auto& k : kernels)
                        k.old = -1;
                }
            }

            fed += b;
            position = 0;
            designTick();
        }
    }
}

void LinearPhaseFilter::processBlockOf (int stage, int ch, int kernelIndex) noexcept
{
    auto& c = channelState[(size_t) stage][(size_t) ch];
    const auto& k = kernels[(size_t) kernelIndex];
    const auto bins = b + 1;

    // Overlap-save: [previous B | this B] -> X, into the delay line.
    for (int i = 0; i < b; ++i)
    {
        blockScratch[(size_t) i] = { c.previous[(size_t) i], 0.0f };
        blockScratch[(size_t) (b + i)] = { c.in[(size_t) i], 0.0f };
    }

    std::copy (c.in.begin(), c.in.begin() + b, c.previous.begin());
    fft.perform (blockScratch.data(), 2 * b, false);
    c.head = (c.head + 1) % p;
    auto* xr = c.fdlRe.data() + c.head * bins;
    auto* xi = c.fdlIm.data() + c.head * bins;

    for (int j = 0; j < bins; ++j)
    {
        xr[j] = blockScratch[(size_t) j].real();
        xi[j] = blockScratch[(size_t) j].imag();
    }

    // Y = sum over partitions of X(t - i) H_i, for the kernel now and, while
    // it fades out, the old one.
    const auto mac = [&] (int set, std::vector<Fft::Complex>& out)
    {
        float yr[1025], yi[1025];   // B + 1 <= 1025 (B is at most 64 x 16)
        std::fill (yr, yr + bins, 0.0f);
        std::fill (yi, yi + bins, 0.0f);
        const auto* hr = k.re[(size_t) set].data();
        const auto* hi = k.im[(size_t) set].data();

        for (int i = 0; i < p; ++i)
        {
            const auto slot = (c.head - i + p) % p;
            const auto* ar = c.fdlRe.data() + slot * bins;
            const auto* ai = c.fdlIm.data() + slot * bins;
            const auto* br = hr + i * bins;
            const auto* bi = hi + i * bins;

            for (int j = 0; j < bins; ++j)
            {
                yr[j] += ar[j] * br[j] - ai[j] * bi[j];
                yi[j] += ar[j] * bi[j] + ai[j] * br[j];
            }
        }

        for (int j = 0; j < bins; ++j)
            out[(size_t) j] = { yr[j], yi[j] };

        for (int j = 1; j < b; ++j)
            out[(size_t) (2 * b - j)] = std::conj (out[(size_t) j]);

        fft.perform (out.data(), 2 * b, true);
    };

    mac (k.current, blockOut);
    const auto scale = 1.0f / (float) (2 * b);

    if (fading && k.old >= 0)
    {
        mac (k.old, blockOld);

        for (int i = 0; i < b; ++i)
        {
            const auto ramp = std::min (1.0f, (float) (fadePosition + i + 1) / (float) fadeLength);
            const auto y = blockOut[(size_t) (b + i)].real(), o = blockOld[(size_t) (b + i)].real();
            c.out[(size_t) i] = (o + (y - o) * ramp) * scale;
        }

        return;
    }

    for (int i = 0; i < b; ++i)
        c.out[(size_t) i] = blockOut[(size_t) (b + i)].real() * scale;
}

//==============================================================================
void LinearPhaseFilter::designTick() noexcept
{
    if (phase == Phase::idle && dirty != 0)
        startFrame();

    auto budget = budgetPerBlock;

    while (budget > 0 && phase != Phase::idle && phase != Phase::waiting)
        if (designStep (budget))
            phase = Phase::waiting;

    mostWork = std::max (mostWork, budgetPerBlock - budget);

    if (phase == Phase::waiting && ! fading)
    {
        finishFrame();
        phase = Phase::idle;
    }
}

void LinearPhaseFilter::startFrame() noexcept
{
    frameSlots = dirty;
    dirty = 0;
    designKernel.fill (false);
    auto leftOrRight = false;

    for (int s = 0; s < maxSlots; ++s)
        if (slots[(size_t) s].active && (slots[(size_t) s].target == Target::left || slots[(size_t) s].target == Target::right))
            leftOrRight = true;

    const auto bins = n / 2 + 1;

    for (int s = 0; s < maxSlots; ++s)
    {
        if ((frameSlots >> s & 1) == 0)
            continue;

        snapshot[(size_t) s] = slots[(size_t) s];

        if (slots[(size_t) s].binGains)
            std::copy (binGainStore.begin() + s * maxBins, binGainStore.begin() + s * maxBins + bins,
                       binGainSnapshot.begin() + s * maxBins);

        for (int k = 0; k < numKernels; ++k)
            if (kernelHas (k, slots[(size_t) s].target))
                designKernel[(size_t) k] = true;
    }

    // Kernels outside the stages in use aren't played.
    if (stages == Stages::ms || numChannels == 1)
        designKernel[0] = designKernel[1] = numChannels == 1 ? designKernel[0] : false;

    if (stages == Stages::lr || numChannels == 1)
        designKernel[2] = designKernel[3] = false;

    // Stereo with no Left or Right band: R is L's kernel, designed once.
    copyRight = false;

    if (numChannels == 2 && stages != Stages::ms && ! leftOrRight && (designKernel[0] || designKernel[1]))
    {
        designKernel[0] = true;
        designKernel[1] = false;
        copyRight = true;
    }

    for (int k = 0; k < numKernels; ++k)
        nextSet[(size_t) k] = freeSet (kernels[(size_t) k]);

    phase = Phase::slots;
    designSlot = 0;
    designPosition = 0;

    while (designSlot < maxSlots && (frameSlots >> designSlot & 1) == 0)
        ++designSlot;
}

bool LinearPhaseFilter::designStep (int& budget) noexcept
{
    const auto bins = n / 2 + 1;
    const auto table = (size_t) resolution;

    switch (phase)
    {
        case Phase::slots:
        {
            if (designSlot >= maxSlots)
            {
                phase = Phase::sum;
                designKernelIndex = 0;
                designPosition = 0;

                while (designKernelIndex < numKernels && ! designKernel[(size_t) designKernelIndex])
                    ++designKernelIndex;

                if (designKernelIndex >= numKernels)
                    return true;   // nothing to design (e.g. a band outside the stages)

                return false;
            }

            const auto& slot = snapshot[(size_t) designSlot];
            auto sections = 0;

            for (int d = 0; d < slot.numDesigns; ++d)
                sections += slot.designs[(size_t) d].numSections;

            const auto cost = 1 + sections;
            const auto count = std::max (1, budget / cost);
            const auto end = std::min (bins, designPosition + count);
            auto* power = slotPower.data() + designSlot * maxBins;
            const auto* gains = binGainSnapshot.data() + designSlot * maxBins;

            for (int bin = designPosition; bin < end; ++bin)
            {
                auto pw = 1.0;

                if (slot.active)
                {
                    if (slot.brickwall != Brickwall::none)
                    {
                        const auto f = bin * rate / n;
                        const auto pass = slot.brickwall == Brickwall::lowCut ? f >= slot.brickwallFrequency
                                                                              : f <= slot.brickwallFrequency;
                        pw = pass ? 1.0 : stopPower;
                    }
                    else
                    {
                        const auto c1 = cos1[table][(size_t) bin], c2 = cos2[table][(size_t) bin];

                        for (int d = 0; d < slot.numDesigns; ++d)
                        {
                            const auto& design = slot.designs[(size_t) d];

                            for (int s = 0; s < design.numSections; ++s)
                            {
                                const auto& q = design.sections[(size_t) s];
                                const auto num = q.b0 * q.b0 + q.b1 * q.b1 + q.b2 * q.b2
                                                 + 2.0 * (q.b0 * q.b1 + q.b1 * q.b2) * c1 + 2.0 * q.b0 * q.b2 * c2;
                                const auto den = 1.0 + q.a1 * q.a1 + q.a2 * q.a2
                                                 + 2.0 * (q.a1 + q.a1 * q.a2) * c1 + 2.0 * q.a2 * c2;
                                pw *= std::max (num, 0.0) / std::max (den, 1.0e-30);
                            }
                        }
                    }

                    if (slot.binGains)
                        pw *= std::pow (10.0, (double) gains[bin] / 10.0);
                }

                power[bin] = (float) std::clamp (pw, 1.0e-30, 1.0e30);
            }

            budget -= (end - designPosition) * cost;
            designPosition = end;

            if (designPosition >= bins)
            {
                designPosition = 0;
                ++designSlot;

                while (designSlot < maxSlots && (frameSlots >> designSlot & 1) == 0)
                    ++designSlot;
            }

            return false;
        }

        case Phase::sum:
        {
            // The kernel's magnitude: the product of its slots, a real even spectrum.
            const auto k = designKernelIndex;
            std::array<int, maxSlots> members {};
            auto count = 0;

            for (int s = 0; s < maxSlots; ++s)
                if (slots[(size_t) s].active && kernelHas (k, slots[(size_t) s].target))
                    members[(size_t) count++] = s;

            const auto cost = 1 + count / 4;
            const auto end = std::min (bins, designPosition + std::max (1, budget / cost));

            for (int bin = designPosition; bin < end; ++bin)
            {
                auto pw = 1.0;

                for (int i = 0; i < count; ++i)
                    pw *= (double) slotPower[(size_t) (members[(size_t) i] * maxBins + bin)];

                const auto amplitude = (float) std::sqrt (std::clamp (pw, 1.0e-30, 1.0e30));
                designBuffer[(size_t) bin] = { amplitude, 0.0f };

                if (bin > 0 && bin < n / 2)
                    designBuffer[(size_t) (n - bin)] = { amplitude, 0.0f };
            }

            budget -= (end - designPosition) * cost;
            designPosition = end;

            if (designPosition >= bins)
            {
                fft.start (inverseJob, designBuffer.data(), n, true);
                phase = Phase::inverse;
            }

            return false;
        }

        case Phase::inverse:
        {
            budget -= std::max (1, fft.step (inverseJob, budget));

            if (inverseJob.done)
            {
                phase = Phase::window;
                designPosition = 0;
            }

            return false;
        }

        case Phase::window:
        {
            auto& k = kernels[(size_t) designKernelIndex];
            auto& taps = k.taps[(size_t) nextSet[(size_t) designKernelIndex]];
            const auto& w = windows[table];
            const auto end = std::min (n, designPosition + std::max (1, 2 * budget));
            const auto norm = 1.0f / (float) n;

            for (int t = designPosition; t < end; ++t)
                taps[(size_t) t] = w[(size_t) t] * designBuffer[(size_t) ((t - n / 2 + n) % n)].real() * norm;

            budget -= (end - designPosition + 1) / 2;
            designPosition = end;

            if (designPosition >= n)
            {
                phase = Phase::partitions;
                designPosition = 0;
            }

            return false;
        }

        case Phase::partitions:
        {
            auto& k = kernels[(size_t) designKernelIndex];
            const auto set = (size_t) nextSet[(size_t) designKernelIndex];
            const auto& taps = k.taps[set];
            const auto part = designPosition;

            for (int i = 0; i < b; ++i)
            {
                blockScratch[(size_t) i] = { taps[(size_t) (part * b + i)], 0.0f };
                blockScratch[(size_t) (b + i)] = {};
            }

            fft.perform (blockScratch.data(), 2 * b, false);
            auto* re = k.re[set].data() + part * (b + 1);
            auto* im = k.im[set].data() + part * (b + 1);

            for (int j = 0; j <= b; ++j)
            {
                re[j] = blockScratch[(size_t) j].real();
                im[j] = blockScratch[(size_t) j].imag();
            }

            budget -= Fft::unitsFor (2 * b);

            if (++designPosition >= p)
            {
                designPosition = 0;
                ++designKernelIndex;

                while (designKernelIndex < numKernels && ! designKernel[(size_t) designKernelIndex])
                    ++designKernelIndex;

                if (designKernelIndex >= numKernels)
                    return true;

                phase = Phase::sum;
            }

            return false;
        }

        case Phase::idle:
        case Phase::waiting:
        default:
            return true;
    }
}

void LinearPhaseFilter::finishFrame() noexcept
{
    for (int k = 0; k < numKernels; ++k)
    {
        auto& kernel = kernels[(size_t) k];

        if (designKernel[(size_t) k])
        {
            kernel.old = kernel.current;
            kernel.current = nextSet[(size_t) k];
        }
        else
        {
            kernel.old = -1;
        }
    }

    if (copyRight)
    {
        auto& left = kernels[0];
        auto& right = kernels[1];
        const auto set = (size_t) freeSet (right);
        const auto from = (size_t) left.current;
        std::copy (left.re[from].begin(), left.re[from].begin() + p * (b + 1), right.re[set].begin());
        std::copy (left.im[from].begin(), left.im[from].begin() + p * (b + 1), right.im[set].begin());
        std::copy (left.taps[from].begin(), left.taps[from].begin() + n, right.taps[set].begin());
        right.old = right.current;
        right.current = (int) set;
    }

    fading = true;
    fadePosition = 0;
    ++framesDesigned;
}

double LinearPhaseFilter::kernelResponseDb (int kernel, double frequency) const noexcept
{
    const auto& k = kernels[(size_t) std::clamp (kernel, 0, numKernels - 1)];
    const auto& taps = k.taps[(size_t) k.current];
    const auto w = 2.0 * pi * frequency / rate;
    double re = 0.0, im = 0.0;

    for (int t = 0; t < n; ++t)
    {
        re += taps[(size_t) t] * std::cos (w * t);
        im -= taps[(size_t) t] * std::sin (w * t);
    }

    return 10.0 * std::log10 (std::max (re * re + im * im, 1.0e-30));
}

} // namespace encore::dsp
