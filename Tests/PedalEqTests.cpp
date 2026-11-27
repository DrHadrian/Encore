// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The Pedal EQ "G7" (FX Unit, stage B1, decision 0065): its parameters and
// its sound, measured against its maths.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalEqBlock.h"
#include "engine/core/FactoryPresets.h"
#include "engine/core/Realtime.h"
#include "engine/dsp/Oversampler.h"

#include <juce_dsp/juce_dsp.h>

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

    /** A sine at a level (peak dBFS) for `seconds`. */
    std::vector<float> toneAt (double frequency, float peakDbfs, double rate, double seconds)
    {
        const auto n = (int) (seconds * rate);
        const auto a = juce::Decibels::decibelsToGain (peakDbfs);
        std::vector<float> s ((size_t) n);

        for (int i = 0; i < n; ++i)
            s[(size_t) i] = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / rate);

        return s;
    }

    /** Blackman-Harris windowed copy of the second half (settled). */
    std::vector<float> settledWindow (const std::vector<float>& x)
    {
        std::vector<float> w (x.begin() + (long) x.size() / 2, x.end());

        for (size_t i = 0; i < w.size(); ++i)
        {
            const auto t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) (w.size() - 1);
            w[i] *= (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t));
        }

        return w;
    }

    /** Total harmonic distortion (harmonics 2..10 against the fundamental), dB. */
    double thdDb (const std::vector<float>& x, double fundamental, double rate)
    {
        const auto w = settledWindow (x);
        const auto power = [&] (double f) { return std::pow (10.0, dftDb (w, f, rate) / 10.0); };
        auto harmonics = 0.0;

        for (int h = 2; h <= 10 && h * fundamental < 0.45 * rate; ++h)
            harmonics += power (h * fundamental);

        return 10.0 * std::log10 (std::max (harmonics, 1.0e-30) / power (fundamental));
    }

    /** Aliasing of a block: a 0 dBFS sine exactly on FFT bin `bin` (of 32768
        at 48 kHz) through it; the power in every bin that isn't a harmonic,
        relative to the total (dB). */
    double aliasingDb (Block& block, int bin, float amplitude = 1.0f)
    {
        constexpr int order = 15, size = 1 << order;
        const auto frequency = bin * sampleRate / size;
        std::vector<float> x ((size_t) size * 2);

        for (size_t i = 0; i < x.size(); ++i)
            x[i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * (double) i / sampleRate);

        const auto out = play (block, x);
        std::vector<float> fft ((size_t) size * 2, 0.0f);
        std::copy (out.begin() + size, out.end(), fft.begin());
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (fft.data());

        double total = 0.0, harmonics = 0.0;

        for (int b = 1; b < size / 2; ++b)
        {
            const auto p = (double) fft[(size_t) b] * fft[(size_t) b];
            total += p;

            if (b % bin == 0 || b <= 2)   // harmonics (and DC leakage from the asymmetry)
                harmonics += p;
        }

        return 10.0 * std::log10 (std::max (total - harmonics, 1.0e-30) / total);
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

                    // A small impulse: the bands' boosts stay below the soft clip's knee
                    // (the clip is in the path, since sliders are up: linear there).
                    constexpr float size = 0.01f;
                    auto impulse = test::impulse ((int) (1.5 * rate), 0, size);   // long enough for the DC blocker's tail
                    auto response = play (eq, impulse, rate);

                    for (auto& v : response)
                        v /= size;
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

                for (int i = 0; i < n; ++i)   // quiet enough that +15 dB stays below the clip's knee
                    tone[(size_t) i] = 0.02f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate);

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

        beginTest ("Input: Pad -6 dB and Boost +6 dB, exact; Level +/-15 dB after the bands");
        {
            const auto rmsDb = [] (const std::vector<float>& v) { return toDb (rms (v, (int) v.size() / 2, (int) v.size())); };
            const auto quiet = sine (440.0, 0.05f, 9600);   // -26 dBFS: far below the soft clip
            const auto dry = rmsDb (quiet);

            struct Case { float input; float level; double expectedDb; };

            for (const auto& c : { Case { 0.0f, 0.0f, -6.0 }, Case { 2.0f, 0.0f, 6.0 }, Case { 1.0f, -15.0f, -15.0 },
                                   Case { 1.0f, 15.0f, 15.0 }, Case { 0.0f, 9.0f, 3.0 } })
            {
                PedalEqBlock eq;
                eq.findParameter ("input")->setValue (c.input);
                eq.findParameter ("level")->setValue (c.level);
                expectWithinAbsoluteError ((double) (rmsDb (play (eq, quiet)) - dry), c.expectedDb, 0.01,
                                           "input " + juce::String (c.input) + ", level " + juce::String (c.level));
            }

            expectWithinAbsoluteError (PedalEqBlock::inputGainFor (PedalEqBlock::Input::pad), 0.501187f, 1.0e-6f);
            expectWithinAbsoluteError (PedalEqBlock::inputGainFor (PedalEqBlock::Input::boost), 1.995262f, 1.0e-6f);
            expectEquals (PedalEqBlock::inputGainFor (PedalEqBlock::Input::normal), 1.0f);

            // Pad with Level +6: the gains are exact sample by sample (no ramp left).
            PedalEqBlock eq;
            eq.findParameter ("input")->setValue (0.0f);
            eq.findParameter ("level")->setValue (6.0f);
            const auto input = noise (0.05f, 4800, 8);
            const auto out = play (eq, input);
            const auto gain = PedalEqBlock::inputGainFor (PedalEqBlock::Input::pad) * juce::Decibels::decibelsToGain (6.0f);
            auto worst = 0.0f;

            for (size_t i = 0; i < input.size(); ++i)
                worst = std::max (worst, std::abs (out[i] - input[i] * gain));

            expectLessThan (worst, 1.0e-7f);
        }

        beginTest ("Switching Input moves the gain over 10 ms: no jump");
        {
            PedalEqBlock eq;
            auto* in = eq.findParameter ("input");
            const auto tone = sine (220.0, 0.05f, 19200);
            auto maxStep = 0.0f, maxToneStep = 0.0f;

            for (size_t i = 1; i < tone.size(); ++i)
                maxToneStep = std::max (maxToneStep, std::abs (tone[i] - tone[i - 1]));

            const auto out = play (eq, tone, sampleRate, 64, [&] (int start)
            {
                if (start == 4800) in->setValue (2.0f);
                if (start == 9600) in->setValue (0.0f);
                if (start == 14400) in->setValue (1.0f);
            });

            for (size_t i = 1; i < out.size(); ++i)
                maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));

            expectLessThan (maxStep, maxToneStep * 2.2f, "a hard switch would jump");
        }

        beginTest ("The soft clip: THD below -80 dB at -12 dBFS with Boost");
        {
            for (auto rate : rates)
            {
                PedalEqBlock eq;
                eq.findParameter ("input")->setValue (2.0f);   // Boost: the clip is in the path
                const auto out = play (eq, toneAt (1000.0, -12.0f, rate, 1.0), rate);
                expect (eq.isClipInPath());
                expectLessThan (thdDb (out, 1000.0, rate), -80.0, juce::String (rate));
                expect (! eq.isHot());
            }
        }

        beginTest ("The soft clip compresses gently above the knee (the stated curve)");
        {
            // Internal peak level (after Input Boost) -> gain change of the peak.
            // The curve (dsp::SoftClip): 0 below -6 dBFS, about -0.1 dB at 0 dBFS,
            // -1 dB at +6 dBFS, -4.3 dB at +12 dBFS.
            struct Point { float inputDbfs; double expectedDb; double tolerance; };

            for (const auto& p : { Point { -12.0f, 0.0, 0.02 }, Point { -6.0f, -0.09, 0.05 },
                                   Point { 0.0f, -1.0, 0.15 }, Point { 6.0f, -4.3, 0.3 } })
            {
                PedalEqBlock eq;
                eq.findParameter ("input")->setValue (2.0f);
                const auto in = toneAt (200.0, p.inputDbfs, sampleRate, 0.5);
                const auto out = play (eq, in, sampleRate);
                const auto change = (double) (toDb (peak (out, (int) out.size() / 2, (int) out.size()))
                                              - (p.inputDbfs + 6.0f));
                expectWithinAbsoluteError (change, p.expectedDb, p.tolerance, juce::String (p.inputDbfs) + " dBFS in");
            }
        }

        beginTest ("Aliasing of the clip falls with Quality");
        {
            // Slammed: Boost, every slider at +15 and a +24 dBFS 2.5 kHz tone,
            // so the clip works hard. (Driven only gently, its smooth curve
            // aliases below the measurement floor at every factor.)
            const auto aliasing = [] (int qualityIndex)
            {
                PedalEqBlock eq;
                eq.findParameter ("input")->setValue (2.0f);
                setBands (eq, { 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f });
                eq.findParameter ("quality")->setValue ((float) qualityIndex);
                return aliasingDb (eq, 1707, juce::Decibels::decibelsToGain (24.0f));   // 2.5 kHz, on a bin
            };

            const auto live = aliasing (0), high = aliasing (1), max = aliasing (2);
            logMessage ("Pedal EQ clip aliasing (dB below the signal): Live " + juce::String (live, 1)
                        + ", High " + juce::String (high, 1) + ", Max " + juce::String (max, 1));
            expectLessThan (high, live - 3.0);
            expectLessThan (max, high - 3.0);
        }

        beginTest ("HOT: on while the clip takes off more than 1 dB, off 300 ms after");
        {
            PedalEqBlock eq;
            eq.findParameter ("input")->setValue (2.0f);
            eq.prepare ({ sampleRate, 64 });
            eq.reset();

            const auto loud = toneAt (200.0, 3.0f, sampleRate, 0.2);    // +9 dBFS inside: works the clip
            const auto quiet = toneAt (200.0, -20.0f, sampleRate, 0.6);
            std::vector<bool> hotness;

            for (const auto* signal : { &loud, &quiet })
            {
                auto copy = *signal;

                for (int start = 0; start + 64 <= (int) copy.size(); start += 64)
                {
                    float* channels[1] = { copy.data() + start };
                    eq.process (channels, 1, 64, {});
                    hotness.push_back (eq.isHot());
                }
            }

            const auto buffersPerSecond = sampleRate / 64.0;
            const auto loudBuffers = (int) (0.2 * buffersPerSecond);
            expect (hotness[(size_t) loudBuffers / 2], "hot while it clips");
            expect (hotness[(size_t) (loudBuffers + 0.25 * buffersPerSecond)], "still hot 250 ms after");
            expect (! hotness[(size_t) (loudBuffers + 0.32 * buffersPerSecond)], "off 320 ms after");
            expect (! hotness.back());

            // Quiet with Boost never lights it.
            PedalEqBlock calm;
            calm.findParameter ("input")->setValue (2.0f);
            play (calm, toneAt (200.0, -20.0f, sampleRate, 0.5));
            expect (! calm.isHot());
        }

        beginTest ("Latency: the oversampling's while the clip is in the path, 0 otherwise; the change crossfades");
        {
            for (int q = 0; q < 3; ++q)
            {
                dsp::Oversampler reference;
                reference.setQuality ((dsp::Quality) q);
                reference.prepare (1, 64, sampleRate);

                PedalEqBlock eq;
                eq.findParameter ("quality")->setValue ((float) q);
                auto* in = eq.findParameter ("input");
                const auto tone = toneAt (220.0, -20.0f, sampleRate, 0.6);
                std::vector<int> latencies;
                const auto out = play (eq, tone, sampleRate, 64, [&] (int start)
                {
                    if (start == 9600) in->setValue (2.0f);    // Boost: the clip comes in
                    if (start == 19200) in->setValue (1.0f);   // Normal: it goes out again
                    latencies.push_back (eq.getLatencySamples());
                });

                expectEquals (latencies[100], 0, "flat: no clip, no latency");
                expectEquals (latencies[250], reference.getLatencySamples(), "Boost: the oversampling's latency");
                expectGreaterThan (reference.getLatencySamples(), 0);
                expectEquals (latencies.back(), 0, "back to Normal: 0 again");

                // No jump where the clip comes in or goes out.
                auto maxStep = 0.0f, maxToneStep = 0.0f;

                for (size_t i = 1; i < out.size(); ++i)
                {
                    maxStep = std::max (maxStep, std::abs (out[i] - out[i - 1]));
                    maxToneStep = std::max (maxToneStep, std::abs (tone[i] - tone[i - 1]));
                }

                expectLessThan (maxStep, maxToneStep * 2.2f);
            }
        }

        beginTest ("Real pedal clipping: Off lets a hot input through untouched when flat; On clips it");
        {
            const auto hotInput = toneAt (200.0, 6.0f, sampleRate, 0.5);   // +6 dBFS from a boost before it

            PedalEqBlock off;
            expect (play (off, hotInput) == hotInput, "Off, flat: bit-exact, even this hot");
            expectEquals (off.getLatencySamples(), 0);

            PedalEqBlock on;
            on.findParameter ("realClip")->setValue (1.0f);
            const auto out = play (on, hotInput);
            expect (on.isClipInPath());
            expectGreaterThan (on.getLatencySamples(), 0);
            expectWithinAbsoluteError ((double) toDb (peak (out, (int) out.size() / 2, (int) out.size())), 6.0 - 1.0, 0.15,
                                       "clipped like a 9 V pedal: -1 dB at +6 dBFS");
            expect (on.isHot());
        }

        beginTest ("Order: Input before the clip, Level after it");
        {
            const auto driven = [] (float inputSetting, float levelDb)
            {
                PedalEqBlock eq;
                eq.findParameter ("input")->setValue (inputSetting);
                eq.findParameter ("g800")->setValue (15.0f);
                eq.findParameter ("level")->setValue (levelDb);
                return thdDb (play (eq, toneAt (800.0, -6.0f, sampleRate, 1.0)), 800.0, sampleRate);
            };

            const auto normal = driven (1.0f, 0.0f);
            expectWithinAbsoluteError (driven (1.0f, -15.0f), normal, 0.5, "Level after the clip: same distortion, just quieter");
            expectLessThan (driven (0.0f, 0.0f), normal - 6.0, "Pad before the clip: less distortion");
            expectGreaterThan (driven (2.0f, 0.0f), normal + 3.0, "Boost before the clip: more");
        }

        beginTest ("Factory presets: every one loads with the stated values; Flat is passthrough");
        {
            PedalEqBlock eq;
            juce::StringArray ids, names;

            for (const auto& p : eq.getFactoryPresets())
            {
                ids.add (p.id);
                names.add (p.name);

                for (const auto& v : p.values)
                    expect (eq.findParameter (v.parameterId) != nullptr, juce::String (p.id) + ": " + v.parameterId);
            }

            expectEquals (ids.joinIntoString (","), juce::String ("flat,midPush,scoop,cleanBoost,bright,thicken"));
            expectEquals (names.joinIntoString (","), juce::String ("Flat,Mid push,Scoop,Clean boost,Bright,Thicken"));

            const auto check = [this] (const char* id, std::map<juce::String, float> expected)
            {
                PedalEqBlock block;
                const auto* preset = presets::find (block, id);
                expect (preset != nullptr, id);

                if (preset == nullptr)
                    return;

                const auto values = presets::valuesFor (block, *preset);
                expectEquals ((int) values.size(), 10, "the sliders, Level, Q and Input");

                for (const auto& [param, value] : values)
                {
                    const auto it = expected.find (param);
                    const auto want = it != expected.end() ? it->second : block.findParameter (param)->getSpec().defaultValue;
                    expectEquals (value, want, juce::String (id) + " " + param);
                }
            };

            check ("flat", {});
            check ("midPush", { { "g800", 6.0f }, { "g1k6", 4.0f } });
            check ("scoop", { { "g400", -5.0f }, { "g800", -6.0f } });
            check ("cleanBoost", { { "input", 2.0f }, { "level", 6.0f } });
            check ("bright", { { "g3k2", 4.0f }, { "g6k4", 3.0f } });
            check ("thicken", { { "g100", 3.0f }, { "g200", 4.0f }, { "g6k4", -3.0f } });

            // Flat after another preset: back to bit-exact passthrough.
            PedalEqBlock flat;
            presets::apply (flat, *presets::find (flat, "scoop"));
            presets::apply (flat, *presets::find (flat, "flat"));
            const auto input = noise (0.5f, 4800, 11);
            expect (play (flat, input) == input);
        }

        beginTest ("A preset sets the face, not the setup; unknown ids are refused cleanly");
        {
            PedalEqBlock eq;
            eq.findParameter ("quality")->setValue (2.0f);
            eq.findParameter ("captureLevel")->setValue (1.0f);
            eq.findParameter ("realClip")->setValue (1.0f);
            eq.findParameter ("g3k2")->setValue (-9.0f);
            presets::apply (eq, *presets::find (eq, "midPush"));

            expectEquals (eq.findParameter ("g800")->getValue(), 6.0f);
            expectEquals (eq.findParameter ("g3k2")->getValue(), 0.0f, "a slider the preset doesn't list goes to 0");
            expectEquals (eq.findParameter ("quality")->getChoiceIndex(), 2, "setup untouched");
            expectEquals (eq.findParameter ("captureLevel")->getChoiceIndex(), 1);
            expectEquals (eq.findParameter ("realClip")->getChoiceIndex(), 1);

            expect (presets::find (eq, "nope") == nullptr);
            expect (presets::find (eq, "") == nullptr);
            expect (presets::find (eq, "Mid push") == nullptr, "ids, not names");
            expectEquals (presets::listIds (eq), juce::String ("flat, midPush, scoop, cleanBoost, bright, thicken"));
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
            auto* inputParameter = eq.findParameter ("input");
            auto* levelParameter = eq.findParameter ("level");
            std::vector<float> a ((size_t) blockSize, 0.25f), b ((size_t) blockSize, -0.25f);
            float* channels[2] = { a.data(), b.data() };

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 300; ++i)
                {
                    sliders[(size_t) (i % 7)]->setValue ((float) ((i * 7) % 31 - 15));
                    q->setValue ((float) ((i / 20) % 2));
                    inputParameter->setValue ((float) ((i / 15) % 3));
                    levelParameter->setValue ((float) ((i * 3) % 31 - 15));
                    eq.process (channels, 2, blockSize, {});
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static PedalEqTests pedalEqTests;

} // namespace encore::test
