// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B5: the shared linear-phase filter (dsp::LinearPhaseFilter).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/core/Realtime.h"
#include "engine/dsp/LinearPhaseFilter.h"

#include <chrono>
#include <complex>

namespace encore::test
{

namespace
{
    using Lp = dsp::LinearPhaseFilter;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    dsp::FilterBandDesign design (dsp::FilterShape shape, double f, double gainDb, double q, int slope, double rate)
    {
        return dsp::FilterBandDesign::make ({ shape, f, gainDb, q, slope }, rate);
    }

    void setBand (Lp& lp, int slot, const dsp::FilterBandDesign& d, Lp::Target target = Lp::Target::both)
    {
        auto& s = lp.getSlot (slot);
        s.active = true;
        s.target = target;
        s.designs[0] = d;
        s.numDesigns = 1;
        s.brickwall = Lp::Brickwall::none;
        lp.slotChanged (slot);
    }

    /** Runs silence until everything is designed and the fades are over. */
    void settle (Lp& lp, int channels, int block = 64)
    {
        std::vector<float> l ((size_t) block), r ((size_t) block);

        for (int i = 0; i < 4000 && ! lp.isSettled(); ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            float* ch[] = { l.data(), r.data() };
            lp.process (ch, channels, block);
        }
    }

    /** Plays x (per channel) through, in blocks of 64. */
    void run (Lp& lp, std::vector<float>& l, std::vector<float>* r = nullptr, int block = 64)
    {
        for (size_t at = 0; at < l.size(); at += (size_t) block)
        {
            const auto len = (int) std::min ((size_t) block, l.size() - at);
            float* ch[] = { l.data() + at, r != nullptr ? r->data() + at : nullptr };
            lp.process (ch, r != nullptr ? 2 : 1, len);
        }
    }

    /** The response of an impulse response at f (dB) and its phase. */
    std::complex<double> responseOf (const std::vector<float>& ir, double f, double rate)
    {
        std::complex<double> sum;

        for (size_t t = 0; t < ir.size(); ++t)
            sum += (double) ir[t] * std::polar (1.0, -twoPi * f * (double) t / rate);

        return sum;
    }

    std::vector<float> impulseResponse (Lp& lp, int length, int channels = 1, int channel = 0)
    {
        std::vector<float> l ((size_t) length, 0.0f), r ((size_t) length, 0.0f);
        (channel == 0 ? l : r)[0] = 1.0f;
        run (lp, l, channels == 2 ? &r : nullptr);
        return channel == 0 ? l : r;
    }

    double highBandDb (const std::vector<float>& x, double above, int from, int to, double rate)
    {
        dsp::FilterBand<1> highPass;
        highPass.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, above, 0.0, 0.7071, 96 }, rate));
        std::vector<float> y (x.begin(), x.begin() + to);
        float* ch[] = { y.data() };
        highPass.process (ch, 1, (int) y.size());
        return juce::Decibels::gainToDecibels (rms (y, from, to), -300.0f) - juce::Decibels::gainToDecibels (rms (x, from, to), -300.0f);
    }

    const char* resName (Lp::Resolution r) { return r == Lp::Resolution::low ? "Low" : r == Lp::Resolution::medium ? "Medium" : "High"; }
}

class LinearPhaseFilterTests final : public juce::UnitTest
{
public:
    LinearPhaseFilterTests() : juce::UnitTest ("Linear-phase filter", "Encore") {}

    void runTest() override
    {
        const Lp::Resolution resolutions[] { Lp::Resolution::low, Lp::Resolution::medium, Lp::Resolution::high };

        beginTest ("Sizes and latency: N/2 + B per stage, the same time at every rate");
        {
            expectEquals (Lp::latencyFor (Lp::Resolution::low, Lp::Stages::lr, 48000.0), 192);
            expectEquals (Lp::latencyFor (Lp::Resolution::medium, Lp::Stages::lr, 48000.0), 576);
            expectEquals (Lp::latencyFor (Lp::Resolution::high, Lp::Stages::lr, 48000.0), 2112);
            expectEquals (Lp::latencyFor (Lp::Resolution::high, Lp::Stages::both, 48000.0), 4224);
            expectEquals (Lp::latencyFor (Lp::Resolution::medium, Lp::Stages::ms, 96000.0), 1152);
            expectEquals (Lp::latencyFor (Lp::Resolution::medium, Lp::Stages::lr, 44100.0), 576);
            expectEquals (Lp::latencyFor (Lp::Resolution::high, Lp::Stages::lr, 192000.0), 8448);
        }

        beginTest ("The kernel is the bands' response (within 0.5 dB where the resolution can follow them), every resolution and rate");
        {
            for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                for (auto res : resolutions)
                {
                    Lp lp;
                    lp.prepare (rate);
                    lp.configure (res, Lp::Stages::lr, 1);
                    lp.reset();
                    const auto bin = rate / lp.getKernelLength();

                    // Bands wide enough for this resolution: 10 bins or more.
                    std::vector<dsp::FilterBandDesign> bands {
                        design (dsp::FilterShape::bell, 40 * bin, 6.0, 1.0, 12, rate),
                        design (dsp::FilterShape::lowShelf, 20 * bin, 4.0, 0.7071, 12, rate),
                        design (dsp::FilterShape::highShelf, std::min (0.3 * rate, 200 * bin), -5.0, 0.7071, 12, rate),
                        design (dsp::FilterShape::highCut, 0.3 * rate, 0.0, 0.7071, 24, rate),
                    };

                    for (int i = 0; i < (int) bands.size(); ++i)
                        setBand (lp, i, bands[(size_t) i]);

                    settle (lp, 1);
                    expect (lp.isSettled());

                    auto worst = 0.0;

                    for (double f = 10 * bin; f < 0.4 * rate; f *= 1.05)
                    {
                        const auto err = std::abs (lp.kernelResponseDb (0, f) - dsp::responseDb (bands, f, rate));
                        worst = std::max (worst, err);
                    }

                    logMessage (juce::String (rate / 1000.0, 1) + " kHz, " + resName (res) + ": kernel vs the bands, worst "
                                + juce::String (worst, 3) + " dB above 10 bins (" + juce::String (10 * bin, 0) + " Hz)");
                    expectLessThan (worst, 0.5, juce::String (rate) + " " + resName (res));
                }
            }
        }

        beginTest ("Linear phase: the impulse response is symmetric about the latency; the group delay is the latency");
        {
            for (auto res : resolutions)
            {
                Lp lp;
                lp.prepare (48000.0);
                lp.configure (res, Lp::Stages::lr, 1);
                lp.reset();
                setBand (lp, 0, design (dsp::FilterShape::bell, 2000.0, 9.0, 0.7, 12, 48000.0));
                setBand (lp, 1, design (dsp::FilterShape::highShelf, 8000.0, -6.0, 0.7071, 12, 48000.0));
                settle (lp, 1);
                const auto latency = lp.getLatencySamples();
                const auto ir = impulseResponse (lp, 2 * latency + 256);

                auto peak = 0;

                for (int t = 0; t < (int) ir.size(); ++t)
                    if (std::abs (ir[(size_t) t]) > std::abs (ir[(size_t) peak]))
                        peak = t;

                expectEquals (peak, latency, resName (res));
                auto asymmetry = 0.0, energy = 0.0;

                for (int m = 1; m < lp.getKernelLength() / 2; ++m)
                {
                    asymmetry = std::max (asymmetry, (double) std::abs (ir[(size_t) (latency + m)] - ir[(size_t) (latency - m)]));
                    energy = std::max (energy, (double) std::abs (ir[(size_t) (latency + m)]));
                }

                expectLessThan (asymmetry, 1.0e-5, "symmetric");
                expect (energy > 1.0e-3, "not just a delay");

                // Group delay from the phase slope at a few frequencies.
                for (auto f : { 500.0, 2000.0, 6000.0 })
                {
                    const auto df = 1.0;
                    const auto a = std::arg (responseOf (ir, f, 48000.0)), c = std::arg (responseOf (ir, f + df, 48000.0));
                    auto d = c - a;

                    while (d > juce::MathConstants<double>::pi)  d -= twoPi;
                    while (d < -juce::MathConstants<double>::pi) d += twoPi;

                    const auto delay = -d / (twoPi * df / 48000.0);
                    expectWithinAbsoluteError (delay, (double) latency, 0.5, "group delay at " + juce::String (f));
                }
            }
        }

        beginTest ("A moving band and a 50 ms sweep follow frame by frame: no zipper or step noise");
        {
            for (auto res : resolutions)
            {
                Lp lp;
                lp.prepare (48000.0);
                lp.configure (res, Lp::Stages::lr, 1);
                lp.reset();
                setBand (lp, 0, design (dsp::FilterShape::bell, 300.0, 0.0, 1.0, 12, 48000.0));
                settle (lp, 1);
                const auto frames = lp.getFramesDesigned();

                // A 300 Hz tone; the bell moves -12 -> +12 dB in 50 ms, back, and keeps moving.
                auto x = sine (300.0, 0.25f, 48000);

                for (size_t at = 0; at < x.size(); at += 64)
                {
                    const auto t = (double) at / 48000.0;
                    const auto phase01 = std::fmod (t / 0.1, 1.0);
                    const auto gain = -12.0 + 24.0 * (phase01 < 0.5 ? 2.0 * phase01 : 2.0 - 2.0 * phase01);
                    setBand (lp, 0, design (dsp::FilterShape::bell, 300.0, gain, 1.0, 12, 48000.0));
                    float* ch[] = { x.data() + at };
                    lp.process (ch, 1, 64);
                }

                const auto designed = lp.getFramesDesigned() - frames;
                const auto high = highBandDb (x, 6000.0, 9600, 48000, 48000.0);
                logMessage (juce::String (resName (res)) + ": a bell sweeping 24 dB every 50 ms on 300 Hz: " + juce::String (designed)
                            + " frames a second, energy above 6 kHz " + juce::String (high, 1) + " dB");
                expect (designed > 40, "frame by frame");
                expectLessThan (high, -85.0, resName (res));
            }
        }

        beginTest ("Stages: L/R leaves the other channel a pure delay; M/S leaves S (or M); both add their latencies");
        {
            const auto left = noise (0.5f, 24000, 1), right = noise (0.5f, 24000, 2);

            const auto render = [&] (Lp::Stages st, Lp::Target target, std::vector<float>& l, std::vector<float>& r)
            {
                Lp lp;
                lp.prepare (48000.0);
                lp.configure (Lp::Resolution::medium, st, 2);
                lp.reset();
                setBand (lp, 0, design (dsp::FilterShape::bell, 1500.0, 9.0, 1.0, 12, 48000.0), target);
                settle (lp, 2);
                l = left;
                r = right;
                run (lp, l, &r);
                return lp.getLatencySamples();
            };

            const auto delayedError = [] (const std::vector<float>& out, const std::vector<float>& in, int latency)
            {
                auto worst = 0.0;

                for (size_t i = (size_t) latency; i < out.size(); ++i)
                    worst = std::max (worst, (double) std::abs (out[i] - in[i - (size_t) latency]));

                return worst;
            };

            std::vector<float> l, r;
            auto latency = render (Lp::Stages::lr, Lp::Target::left, l, r);
            expectEquals (latency, 576);
            expectLessThan (delayedError (r, right, latency), 1.0e-5, "Left: R is the input, delayed");
            expectGreaterThan (delayedError (l, left, latency), 0.05);

            latency = render (Lp::Stages::ms, Lp::Target::mid, l, r);
            expectEquals (latency, 576);
            std::vector<float> sIn (left.size()), sOut (left.size());

            for (size_t i = 0; i < left.size(); ++i)
            {
                sIn[i] = 0.5f * (left[i] - right[i]);
                sOut[i] = 0.5f * (l[i] - r[i]);
            }

            expectLessThan (delayedError (sOut, sIn, latency), 1.0e-5, "Mid: S is the input's S, delayed");

            latency = render (Lp::Stages::both, Lp::Target::side, l, r);
            expectEquals (latency, 1152, "two stages: twice the latency");
            std::vector<float> mIn (left.size()), mOut (left.size());

            for (size_t i = 0; i < left.size(); ++i)
            {
                mIn[i] = 0.5f * (left[i] + right[i]);
                mOut[i] = 0.5f * (l[i] + r[i]);
            }

            expectLessThan (delayedError (mOut, mIn, latency), 1.0e-5, "Side through both stages: M delayed by both");
        }

        beginTest ("Brickwall: the steepest the kernel allows; transition and rejection per resolution");
        {
            for (auto res : resolutions)
            {
                Lp lp;
                lp.prepare (48000.0);
                lp.configure (res, Lp::Stages::lr, 1);
                lp.reset();
                auto& s = lp.getSlot (0);
                s.active = true;
                s.brickwall = Lp::Brickwall::lowCut;
                s.brickwallFrequency = 2000.0;
                lp.slotChanged (0);
                settle (lp, 1);

                const auto bin = 48000.0 / lp.getKernelLength();
                double minus1 = 0.0, minus60 = 0.0, rejection = -300.0;

                for (double f = 2000.0 + 8 * bin; f > 100.0; f -= bin / 20.0)
                {
                    const auto r = lp.kernelResponseDb (0, f);

                    if (minus1 == 0.0 && r < -1.0)
                        minus1 = f;

                    if (minus60 == 0.0 && r < -60.0)
                        minus60 = f;
                }

                for (double f = minus60 - bin; f > 50.0; f -= bin / 4.0)
                    rejection = std::max (rejection, lp.kernelResponseDb (0, f));

                const auto width = minus1 - minus60;
                logMessage (juce::String (resName (res)) + " Brickwall at 2 kHz: -1 dB at " + juce::String (minus1, 0) + " Hz, -60 dB at "
                            + juce::String (minus60, 0) + " Hz: " + juce::String (width, 0) + " Hz (" + juce::String (width / bin, 1)
                            + " bins); below it at most " + juce::String (rejection, 1) + " dB");
                expect (minus60 > 0.0, "reaches -60 dB");
                expectLessThan (width / bin, 4.0);
                expectLessThan (rejection, -58.0);
                expectGreaterThan (lp.kernelResponseDb (0, 4000.0), -0.1, "the passband");
            }
        }

        beginTest ("Fixed work per callback: the worst 64-sample callback, High, two stages, stereo, moving all the time; no allocation");
        {
            Lp lp;
            lp.prepare (48000.0);
            lp.configure (Lp::Resolution::high, Lp::Stages::both, 2);
            lp.reset();

            for (int s = 0; s < 8; ++s)
                setBand (lp, s, design (dsp::FilterShape::bell, 100.0 * (s + 1), 3.0, 1.0, 12, 48000.0),
                         s < 2 ? Lp::Target::left : s < 4 ? Lp::Target::right : s < 6 ? Lp::Target::mid : Lp::Target::side);

            settle (lp, 2);
            std::vector<dsp::FilterBandDesign> moving;

            for (int i = 0; i < 64; ++i)
                moving.push_back (design (dsp::FilterShape::bell, 800.0, -12.0 + 0.375 * i, 1.0, 12, 48000.0));

            auto l = noise (0.5f, 64, 3), r = noise (0.5f, 64, 4);
            double worst = 0.0, total = 0.0;
            const auto callbacks = 3000;
            std::vector<double> times ((size_t) callbacks);
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int c = 0; c < callbacks; ++c)
                {
                    for (auto slot : { 0, 2, 4, 6 })
                    {
                        auto& s = lp.getSlot (slot);
                        s.designs[0] = moving[(size_t) ((c + slot) % 64)];
                        lp.slotChanged (slot);
                    }

                    float* ch[] = { l.data(), r.data() };
                    const auto start = std::chrono::steady_clock::now();
                    lp.process (ch, 2, 64);
                    const auto us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count();
                    worst = std::max (worst, us);
                    total += us;
                    times[(size_t) c] = us;
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);

            auto sorted = times;
            std::sort (sorted.begin(), sorted.end());
            const auto p999 = sorted[(size_t) (callbacks * 999 / 1000)];
            const auto bufferUs = 64.0 / 48000.0 * 1.0e6;
            logMessage ("High, both stages, stereo, 4 bands moving every callback: average " + juce::String (total / callbacks, 1)
                        + " us (" + juce::String (100.0 * total / callbacks / bufferUs, 2) + " %), worst "
                        + juce::String (worst, 1) + " us (" + juce::String (100.0 * worst / bufferUs, 1) + " %), 99.9th percentile "
                        + juce::String (p999, 1) + " us (" + juce::String (100.0 * p999 / bufferUs, 1) + " %) of a "
                        + juce::String (bufferUs, 0) + " us buffer (the worst includes the machine's own interruptions)");
            expectLessThan (p999, 0.25 * bufferUs, "no callback takes a spike from the design");
        }
    }
};

static LinearPhaseFilterTests linearPhaseFilterTests;

} // namespace encore::test
