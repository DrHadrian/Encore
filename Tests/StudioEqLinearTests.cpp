// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B5: Linear phase in the Studio EQ (EQ-PLAN §2.3).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/SplitBlocks.h"
#include "engine/blocks/StudioEqBlock.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"

#include <complex>

namespace encore::test
{

namespace
{
    using Eq = StudioEqBlock;
    using Lp = dsp::LinearPhaseFilter;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    void set (Block& b, const juce::String& id, float v) { b.findParameter (id)->setValue (v); }
    void setSlot (Block& b, int slot, const char* suffix, float v) { set (b, Eq::slotId (slot, suffix), v); }

    void band (Eq& eq, int slot, Eq::Shape shape, float f, float gain, float q = 1.0f, int slopeIndex = 1)
    {
        setSlot (eq, slot, "Used", 1.0f);
        setSlot (eq, slot, "Shape", (float) shape);
        setSlot (eq, slot, "Freq", f);
        setSlot (eq, slot, "Gain", gain);
        setSlot (eq, slot, "Q", q);
        setSlot (eq, slot, "Slope", (float) slopeIndex);
    }

    void start (Block& b, int maxBlock = 64)
    {
        b.prepare ({ sampleRate, maxBlock });
        b.reset();
    }

    std::vector<float> play (Block& b, std::vector<float> x, int chunk = 64, const std::function<void (int)>& before = {})
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            if (before)
                before (offset);

            float* ch[] = { x.data() + offset };
            b.process (ch, 1, juce::jmin (chunk, (int) x.size() - offset), {});
        }

        return x;
    }

    double responseDb (const std::vector<float>& ir, int from, double f)
    {
        std::complex<double> sum;

        for (size_t t = (size_t) from; t < ir.size(); ++t)
            sum += (double) ir[t] * std::polar (1.0, -twoPi * f * (double) (t - (size_t) from) / sampleRate);

        return 20.0 * std::log10 (std::max (std::abs (sum), 1.0e-15));
    }

    double highBandDb (const std::vector<float>& x, double above, int from, int to)
    {
        dsp::FilterBand<1> highPass;
        highPass.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, above, 0.0, 0.7071, 96 }, sampleRate));
        std::vector<float> y (x.begin(), x.begin() + to);
        float* ch[] = { y.data() };
        highPass.process (ch, 1, (int) y.size());
        return juce::Decibels::gainToDecibels (rms (y, from, to), -300.0f) - juce::Decibels::gainToDecibels (rms (x, from, to), -300.0f);
    }

    /** Plays silence until the path is steady. */
    void settle (Eq& eq)
    {
        for (int i = 0; i < 2000 && eq.isPathChanging(); ++i)
            play (eq, std::vector<float> (64, 0.0f));

        play (eq, std::vector<float> (48000, 0.0f));   // and the kernel designed, its fades over
    }
}

class StudioEqLinearTests final : public juce::UnitTest
{
public:
    StudioEqLinearTests() : juce::UnitTest ("Studio EQ linear phase", "Encore") {}

    void runTest() override
    {
        beginTest ("Linear phase plays the bands' curve, delayed by the reported latency; Natural plays Zero latency");
        {
            for (auto q : { Eq::LinearQuality::low, Eq::LinearQuality::medium, Eq::LinearQuality::high })
            {
                Eq eq;
                set (eq, "phase", (float) Eq::Phase::linear);
                set (eq, "linearQuality", (float) q);
                band (eq, 0, Eq::Shape::bell, 3000.0f, 6.0f, 0.8f);
                band (eq, 1, Eq::Shape::highShelf, 9000.0f, -4.0f, 0.7071f);
                band (eq, 2, Eq::Shape::lowCut, 30.0f, 0.0f, 0.7071f, 3);
                band (eq, 3, Eq::Shape::lowShelf, 2000.0f, 3.0f, 0.7071f);
                start (eq);
                expect (eq.isLinearPlaying());
                const auto latency = eq.getLatencySamples();
                expectEquals (latency, Lp::latencyFor ((Lp::Resolution) q, Lp::Stages::lr, sampleRate));
                settle (eq);

                std::vector<float> x ((size_t) (latency + 8192), 0.0f);
                x[0] = 1.0f;
                const auto ir = play (eq, x);

                auto peak = 0;

                for (int t = 0; t < (int) ir.size(); ++t)
                    if (std::abs (ir[(size_t) t]) > std::abs (ir[(size_t) peak]))
                        peak = t;

                expectEquals (peak, latency, "the impulse's centre is the latency");

                // Shift the response back by the latency: then it's the zero-phase curve.
                const auto designs = eq.getBandDesigns (sampleRate);
                const auto bin = sampleRate / (double) (Lp::baseLength[(size_t) q]);
                auto worst = 0.0;

                for (double f = 10 * bin; f < 18000.0; f *= 1.06)
                    worst = std::max (worst, std::abs (responseDb (ir, 0, f) - dsp::responseDb (designs, f, sampleRate)));

                logMessage ("Linear phase " + juce::String ((int) q) + ": latency " + juce::String (latency) + " samples, the curve within "
                            + juce::String (worst, 3) + " dB of Zero latency's above " + juce::String (10 * bin, 0) + " Hz");
                expectLessThan (worst, 0.5);
            }

            Eq natural;
            set (natural, "phase", (float) Eq::Phase::natural);
            band (natural, 0, Eq::Shape::bell, 1000.0f, 6.0f);
            start (natural);
            expectEquals (natural.getLatencySamples(), 0);
            expect (! natural.isLinearPlaying());
        }

        beginTest ("Brickwall in Linear phase is the kernel's steepest; in Zero latency it plays 96 dB/oct");
        {
            Eq eq;
            set (eq, "phase", (float) Eq::Phase::linear);
            set (eq, "linearQuality", (float) Eq::LinearQuality::high);
            band (eq, 0, Eq::Shape::lowCut, 1000.0f, 0.0f, 0.7071f, Eq::brickwallSlope);
            start (eq);
            settle (eq);
            const auto latency = eq.getLatencySamples();
            std::vector<float> x ((size_t) (latency + 8192), 0.0f);
            x[0] = 1.0f;
            const auto ir = play (eq, x);
            const auto at900 = responseDb (ir, 0, 900.0), at1100 = responseDb (ir, 0, 1100.0);
            logMessage ("Linear High Brickwall at 1 kHz: " + juce::String (at900, 1) + " dB at 900 Hz, " + juce::String (at1100, 2)
                        + " dB at 1100 Hz");
            expectLessThan (at900, -60.0);
            expectGreaterThan (at1100, -0.5);
        }

        beginTest ("Changing phase mode while playing: a quick dip (no click), the latency follows; Back to Zero latency too");
        {
            Eq eq;
            band (eq, 0, Eq::Shape::bell, 400.0f, 4.0f);
            start (eq);
            expectEquals (eq.getLatencySamples(), 0);
            auto x = sine (200.0, 0.3f, 96000);
            std::vector<int> latency;
            latency.reserve (2000);
            auto* phaseParameter = eq.findParameter ("phase");
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int at = 0; at < 96000; at += 64)
                {
                    if (at == 9600)  phaseParameter->setValue ((float) Eq::Phase::linear);
                    if (at == 48000) phaseParameter->setValue ((float) Eq::Phase::zeroLatency);

                    float* ch[] = { x.data() + at };
                    eq.process (ch, 1, 64, {});
                    latency.push_back (eq.getLatencySamples());
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expectEquals (latency[9600 / 64 - 1], 0);
            expectEquals (latency[40000 / 64], 576, "Linear Medium's latency after the dip");
            expectEquals (latency.back(), 0, "and back");
            const auto click = highBandDb (x, 3000.0, 2400, 96000);
            logMessage ("Zero latency -> Linear -> Zero latency on a 200 Hz tone: energy above 3 kHz " + juce::String (click, 1) + " dB");
            expectLessThan (click, -55.0, "no click");

            // The dip is there: a short quiet stretch, then the tone again.
            auto quietest = 1.0f;

            for (int at = 9600; at < 40000; at += 48)
                quietest = std::min (quietest, (float) rms (x, at, at + 48));

            expectLessThan (quietest, 0.05f, "a dip");
            expectGreaterThan ((double) rms (x, 44000, 47000), 0.2, "the tone comes back");
        }

        beginTest ("A split with a Linear phase EQ on one path stays lined up");
        {
            auto chain = std::make_unique<Chain>();
            chain->addBlock (std::make_unique<SplitBlock>());
            auto eq = std::make_unique<Eq>();
            set (*eq, "phase", (float) Eq::Phase::linear);
            band (*eq, 0, Eq::Shape::bell, 1000.0f, 0.0f);   // flat: a pure delay
            chain->addBlock (std::move (eq));
            chain->addBlock (std::make_unique<MergeBlock>());
            chain->prepare ({ sampleRate, 64 });
            expectEquals (chain->getPathLatencyDifference(), -576);

            // An impulse: one click (both halves at the same time), not two.
            std::vector<float> l (64), r (64), heard;

            for (int block = 0; block < 400; ++block)
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);

                if (block == 200)
                    l[0] = 1.0f;

                float* ch[] = { l.data(), r.data() };
                chain->process (ch, 64);

                if (block >= 200)
                    heard.insert (heard.end(), l.begin(), l.end());
            }

            int peaks = 0;

            for (auto v : heard)
                if (std::abs (v) > 0.1f)
                    ++peaks;

            expectEquals (peaks, 1, "one click");
        }

        beginTest ("A dynamic band in Linear phase acts ahead of a transient (detected on the undelayed input)");
        {
            const auto firstMsDb = [] (Eq::Phase phase)
            {
                Eq eq;
                set (eq, "phase", (float) phase);
                band (eq, 0, Eq::Shape::bell, 2000.0f, 0.0f, 1.0f);
                setSlot (eq, 0, "Mode", (float) Eq::Mode::dynamic);
                setSlot (eq, 0, "Range", -12.0f);
                setSlot (eq, 0, "Thresh", -40.0f);
                setSlot (eq, 0, "Attack", 0.5f);
                setSlot (eq, 0, "Release", 200.0f);
                start (eq);
                settle (eq);
                const auto latency = eq.getLatencySamples();

                // Silence, then a 2 kHz burst well over the threshold.
                std::vector<float> x (24000, 0.0f);
                const auto burst = sine (2000.0, 0.5f, 12000);
                std::copy (burst.begin(), burst.end(), x.begin() + 4800);
                const auto y = play (eq, x);
                const auto at = 4800 + latency;
                const auto first = rms (y, at, at + 96), later = rms (y, at + 4800, at + 9600);
                return std::pair { juce::Decibels::gainToDecibels (first / 0.3536), juce::Decibels::gainToDecibels (later / 0.3536) };
            };

            const auto zero = firstMsDb (Eq::Phase::zeroLatency), linear = firstMsDb (Eq::Phase::linear);
            logMessage ("A burst's first 2 ms through a dynamic bell (Range -12): Zero latency " + juce::String (zero.first, 1)
                        + " dB, Linear Medium " + juce::String (linear.first, 1) + " dB (settled: " + juce::String (zero.second, 1)
                        + " / " + juce::String (linear.second, 1) + " dB)");
            expectLessThan (linear.first, zero.first - 2.0, "Linear dips the attack that Zero latency lets through");
            expectWithinAbsoluteError (linear.second, zero.second, 1.0, "the same once settled");
        }
    }
};

static StudioEqLinearTests studioEqLinearTests;

} // namespace encore::test
