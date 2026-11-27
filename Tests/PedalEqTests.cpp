// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The Pedal EQ "G7" (FX Unit, stage B1, decision 0063): its parameters and
// its sound, measured against its maths.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalEqBlock.h"
#include "engine/core/Realtime.h"

#include <complex>

namespace encore::test
{

namespace
{
    constexpr double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    /** Plays a mono signal through the block in buffers of `buffer` at `rate`.
        `beforeBuffer (start)` may change settings between buffers. */
    template <typename BeforeBuffer>
    std::vector<float> play (Block& block, std::vector<float> signal, double rate, int buffer, BeforeBuffer&& beforeBuffer)
    {
        block.prepare ({ rate, buffer });
        block.reset();

        for (int start = 0; start < (int) signal.size(); start += buffer)
        {
            beforeBuffer (start);
            const auto length = juce::jmin (buffer, (int) signal.size() - start);
            float* channels[1] = { signal.data() + start };
            block.process (channels, 1, length, {});
        }

        return signal;
    }

    std::vector<float> play (Block& block, std::vector<float> signal, double rate = sampleRate, int buffer = 64)
    {
        return play (block, std::move (signal), rate, buffer, [] (int) {});
    }

    /** |DFT| in dB of a signal at one frequency (an impulse response's magnitude). */
    double dftDb (const std::vector<float>& x, double frequency, double rate)
    {
        std::complex<double> sum, z (1.0, 0.0);
        const auto step = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * frequency / rate);

        for (auto v : x)
        {
            sum += (double) v * z;
            z *= step;
        }

        return 20.0 * std::log10 (std::max (std::abs (sum), 1.0e-15));
    }

    void setBands (Block& block, std::initializer_list<float> gains)
    {
        size_t b = 0;

        for (auto g : gains)
            block.findParameter (PedalEqBlock::bandIds[b++])->setValue (g);
    }
}

class PedalEqTests final : public juce::UnitTest
{
public:
    PedalEqTests() : juce::UnitTest ("Pedal EQ", "Encore") {}

    void runTest() override
    {
        beginTest ("Parameters: ids, order, ranges, defaults and choices");
        {
            PedalEqBlock eq;
            juce::StringArray ids;

            for (const auto& p : eq.getParameters())
                ids.add (p->getSpec().id);

            expectEquals (ids.joinIntoString (","),
                          juce::String ("g100,g200,g400,g800,g1k6,g3k2,g6k4,level,qMode,input,quality,captureLevel,realClip"));

            for (const auto* id : { "g100", "g200", "g400", "g800", "g1k6", "g3k2", "g6k4", "level" })
            {
                const auto& parameterSpec = eq.findParameter (id)->getSpec();
                expectEquals (parameterSpec.range.start, -15.0f, id);
                expectEquals (parameterSpec.range.end, 15.0f, id);
                expectEquals (parameterSpec.defaultValue, 0.0f, id);
                expectEquals (parameterSpec.unit, juce::String ("dB"), id);
                expect (! eq.findParameter (id)->isChoice() && parameterSpec.rampSeconds > 0.0, juce::String (id) + " ramps");
            }

            expectEquals (eq.findParameter ("g1k6")->getSpec().name, juce::String ("1.6 kHz"));
            expectEquals (eq.findParameter ("g100")->getSpec().name, juce::String ("100 Hz"));

            const auto choices = [&eq] (const char* id) { return eq.findParameter (id)->getSpec().choices.joinIntoString (" | "); };
            const auto choiceDefault = [&eq] (const char* id) { return eq.findParameter (id)->getChoiceIndex(); };
            expectEquals (choices ("qMode"), juce::String ("Classic | Wide"));
            expectEquals (choices ("input"), juce::String ("Pad -6 dB | Normal | Boost +6 dB"));
            expectEquals (choices ("quality"), juce::String ("Live | High | Max"));
            expectEquals (choices ("captureLevel"), juce::String ("As captured | Level-matched"));
            expectEquals (choices ("realClip"), juce::String ("Off | On"));
            expectEquals (choiceDefault ("qMode"), 0);
            expectEquals (choiceDefault ("input"), 1);
            expectEquals (choiceDefault ("quality"), 0);
            expectEquals (choiceDefault ("captureLevel"), 0);
            expectEquals (choiceDefault ("realClip"), 0);
        }

        beginTest ("Keeps the channels it gets; no tail");
        {
            PedalEqBlock eq;
            expectEquals (eq.getOutputChannels (1), 1);
            expectEquals (eq.getOutputChannels (2), 2);
            expect (! eq.hasTail());
        }

        beginTest ("Proportional Q: 1.07 at 0 dB, 2.87 at +/-15 dB, linear between; Wide is 1.07");
        {
            using Q = PedalEqBlock::QMode;
            expectEquals (PedalEqBlock::qFor (Q::classic, 0.0), 1.07);
            expectWithinAbsoluteError (PedalEqBlock::qFor (Q::classic, 15.0), 2.87, 1.0e-12);
            expectWithinAbsoluteError (PedalEqBlock::qFor (Q::classic, -15.0), 2.87, 1.0e-12);
            expectWithinAbsoluteError (PedalEqBlock::qFor (Q::classic, 7.5), 1.97, 1.0e-12);
            expectWithinAbsoluteError (PedalEqBlock::qFor (Q::classic, -3.0), 1.07 + 1.8 * 0.2, 1.0e-12);
            expectEquals (PedalEqBlock::qFor (Q::wide, 15.0), 1.07);
            expectEquals (PedalEqBlock::qFor (Q::wide, -6.0), 1.07);
        }

        beginTest ("What you hear is the curve: measured = responseDb of the seven designs, every rate");
        {
            for (auto rate : rates)
            {
                for (auto wide : { false, true })
                {
                    PedalEqBlock eq;
                    setBands (eq, { 12.0f, -9.0f, 6.0f, -15.0f, 15.0f, -3.0f, 8.0f });
                    eq.findParameter ("qMode")->setValue (wide ? 1.0f : 0.0f);

                    auto impulse = test::impulse (1 << 16);
                    const auto response = play (eq, impulse, rate);
                    const auto designs = eq.getBandDesigns (rate);
                    auto worst = 0.0;

                    for (int i = 0; i < 40; ++i)
                    {
                        const auto f = 20.0 * std::pow (20000.0 / 20.0, i / 39.0);
                        worst = std::max (worst, std::abs (dftDb (response, f, rate) - dsp::responseDb (designs, f, rate)));
                    }

                    expectLessThan (worst, 0.02, juce::String (rate) + (wide ? " Wide" : " Classic"));

                    // One band alone reads its own gain at its centre.
                    const auto alone = PedalEqBlock::designFor (4, 15.0, wide ? PedalEqBlock::QMode::wide : PedalEqBlock::QMode::classic, rate);
                    expectWithinAbsoluteError (alone.magnitudeDb (1600.0, rate), 15.0, 0.01);
                }
            }
        }

        beginTest ("Bandwidth at +/-15 dB: Classic Q 2.87, Wide 1.07 (Q = f0 / width at half the gain in dB)");
        {
            const auto measureQ = [] (const dsp::FilterBandDesign& d, double f0, double rate)
            {
                const auto half = 0.5 * d.magnitudeDb (f0, rate);
                const auto edge = [&] (double from, double to)
                {
                    // Bisection in log frequency between a point inside and one outside.
                    for (int i = 0; i < 80; ++i)
                    {
                        const auto mid = std::sqrt (from * to);
                        ((std::abs (d.magnitudeDb (mid, rate)) > std::abs (half)) ? from : to) = mid;
                    }

                    return std::sqrt (from * to);
                };
                return f0 / (edge (f0, f0 * 8.0) - edge (f0, f0 / 8.0));
            };

            using Q = PedalEqBlock::QMode;

            for (auto rate : rates)
            {
                for (int b = 0; b < PedalEqBlock::numBands; ++b)
                {
                    const auto f0 = PedalEqBlock::bandFrequencies[(size_t) b];

                    // The decramped bell follows the analog Q closely; near the top
                    // of a 44.1 kHz band it is a little wider.
                    const auto tolerance = f0 <= rate / 16.0 ? 0.03 : 0.08;

                    for (auto gain : { -15.0, 15.0 })
                    {
                        const auto classic = measureQ (PedalEqBlock::designFor (b, gain, Q::classic, rate), f0, rate);
                        const auto wide = measureQ (PedalEqBlock::designFor (b, gain, Q::wide, rate), f0, rate);
                        const auto label = juce::String (f0) + " Hz " + juce::String (gain) + " dB at " + juce::String (rate);
                        expectWithinAbsoluteError (classic / 2.87, 1.0, tolerance, label + " Classic");
                        expectWithinAbsoluteError (wide / 1.07, 1.0, tolerance, label + " Wide");
                    }
                }
            }
        }

        beginTest ("All at 0 dB, Input Normal: bit-exact passthrough, mono and stereo");
        {
            PedalEqBlock eq;
            const auto input = noise (0.5f, 9600, 3);
            expect (play (eq, input) == input);

            PedalEqBlock stereoEq;
            stereoEq.prepare (spec);
            stereoEq.reset();
            auto left = noise (0.5f, blockSize, 4), right = noise (0.5f, blockSize, 5);
            const auto l0 = left, r0 = right;
            float* channels[2] = { left.data(), right.data() };
            stereoEq.process (channels, 2, blockSize, {});
            expect (left == l0 && right == r0);
        }

        beginTest ("A full-range slider sweep in 50 ms: no zipper noise, stable");
        {
            for (auto rate : rates)
            {
                // A 1 kHz tone while 800 Hz sweeps -15 -> +15 dB in 50 ms. The tone's
                // level moves, so there are sidebands within ~100 Hz of it; zipper
                // noise (steps every 16 samples) would show up kHz away from it.
                const auto n = (int) (0.2 * rate);
                std::vector<float> tone ((size_t) n);

                for (int i = 0; i < n; ++i)
                    tone[(size_t) i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate);

                PedalEqBlock eq;
                auto* slider = eq.findParameter ("g800");
                slider->setValue (-15.0f);
                const auto sweepStart = (int) (0.05 * rate), sweepLength = (int) (0.05 * rate);
                const auto out = play (eq, tone, rate, 64, [&] (int start)
                {
                    const auto t = juce::jlimit (0.0, 1.0, (start - sweepStart) / (double) sweepLength);
                    slider->setValue ((float) (-15.0 + 30.0 * t));
                });

                expect (std::all_of (out.begin(), out.end(), [] (float v) { return std::isfinite (v); }));

                // Junk far from the tone during the sweep, relative to the tone,
                // through a 4-term Blackman-Harris window (sidelobes below -92 dB).
                std::vector<float> window (out.begin() + sweepStart - (int) (0.02 * rate),
                                           out.begin() + sweepStart + sweepLength + (int) (0.02 * rate));

                for (size_t i = 0; i < window.size(); ++i)
                {
                    const auto x = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) (window.size() - 1);
                    window[i] *= (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x));
                }

                auto tonePower = 0.0, junkPower = 0.0;

                for (auto f = 100.0; f < 0.45 * rate && f < 20000.0; f += 25.0)
                {
                    const auto p = std::pow (10.0, dftDb (window, f, rate) / 10.0);
                    (std::abs (f - 1000.0) < 400.0 ? tonePower : junkPower) += p;
                }

                const auto junkDb = 10.0 * std::log10 (junkPower / tonePower);
                logMessage ("Sweep junk at " + juce::String (rate) + ": " + juce::String (junkDb, 1) + " dB");
                expectLessThan (junkDb, -75.0, juce::String (rate));   // -88 dB at 48 kHz measured; 16-sample steps without the glide: -53
            }

            // All seven at once, slammed back and forth: stays finite and bounded.
            PedalEqBlock eq;
            int flips = 0;
            const auto out = play (eq, noise (0.5f, 48000, 6), sampleRate, 64, [&] (int start)
            {
                if (start % 640 == 0)
                    setBands (eq, { (flips % 2) ? 15.0f : -15.0f, (flips % 2) ? -15.0f : 15.0f, 15.0f, -15.0f,
                                    (flips % 2) ? 15.0f : -15.0f, 15.0f, (flips++ % 2) ? -15.0f : 15.0f });
            });
            expect (std::all_of (out.begin(), out.end(), [] (float v) { return std::isfinite (v) && std::abs (v) < 50.0f; }));
        }

        beginTest ("No allocation while playing, moving sliders and switching Q");
        {
            PedalEqBlock eq;
            eq.prepare (spec);
            eq.reset();
            std::array<Parameter*, PedalEqBlock::numBands> sliders {};

            for (size_t b = 0; b < sliders.size(); ++b)
                sliders[b] = eq.findParameter (PedalEqBlock::bandIds[b]);

            auto* q = eq.findParameter ("qMode");
            std::vector<float> a ((size_t) blockSize, 0.25f), b ((size_t) blockSize, -0.25f);
            float* channels[2] = { a.data(), b.data() };

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 300; ++i)
                {
                    sliders[(size_t) (i % 7)]->setValue ((float) ((i * 7) % 31 - 15));
                    q->setValue ((float) ((i / 20) % 2));
                    eq.process (channels, 2, blockSize, {});
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static PedalEqTests pedalEqTests;

} // namespace encore::test
