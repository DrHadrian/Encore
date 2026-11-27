// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B0 (decision 0062): the Encore Engine's shared DSP,
// measured against its maths at every sample rate Encore runs at.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/core/Realtime.h"
#include "engine/dsp/Detector.h"
#include "engine/dsp/FilterBand.h"
#include "engine/dsp/Oversampler.h"

#include <complex>

namespace encore::test
{

namespace
{
    constexpr double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    /** The band's measured response (dB) at a frequency: an impulse through
        the real audio path, then the DFT of the impulse response at that
        frequency. Long enough for 20 Hz bands at 192 kHz to ring out. */
    struct MeasuredResponse
    {
        MeasuredResponse (const dsp::FilterBandDesign& design, int length = 1 << 17)
            : impulse ((size_t) length, 0.0f)
        {
            dsp::FilterBand<1> band;
            band.setDesign (design);
            impulse[0] = 1.0f;
            float* channels[1] = { impulse.data() };
            band.process (channels, 1, length);
        }

        double db (double frequency, double rate) const
        {
            std::complex<double> sum;
            const auto w = twoPi * frequency / rate;
            const auto step = std::polar (1.0, -w);
            std::complex<double> z (1.0, 0.0);

            for (auto x : impulse)
            {
                sum += (double) x * z;
                z *= step;
            }

            return 20.0 * std::log10 (std::max (std::abs (sum), 1.0e-15));
        }

        std::vector<float> impulse;
    };

    std::vector<double> logFrequencies (double from, double to, int count)
    {
        std::vector<double> f;

        for (int i = 0; i < count; ++i)
            f.push_back (from * std::pow (to / from, i / (count - 1.0)));

        return f;
    }

    dsp::FilterSettings band (dsp::FilterShape shape, double frequency, double gainDb = 0.0,
                              double q = 0.7071, int slope = 12)
    {
        return { shape, frequency, gainDb, q, slope };
    }

    juce::String describe (const dsp::FilterSettings& s, double rate)
    {
        return "shape " + juce::String ((int) s.shape) + ", " + juce::String (s.frequency) + " Hz, "
               + juce::String (s.gainDb) + " dB, Q " + juce::String (s.q) + ", "
               + juce::String (s.slopeDbPerOctave) + " dB/oct at " + juce::String (rate) + " Hz";
    }
}

class FilterTests final : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest ("Module filters", "Encore") {}

    void runTest() override
    {
        using S = dsp::FilterShape;

        beginTest ("What you hear is the response function: measured = computed, every shape, every rate");
        {
            for (auto fs : rates)
            {
                const dsp::FilterSettings cases[] = {
                    band (S::bell, 1000.0, 12.0, 1.0),
                    band (S::bell, 8000.0, -18.0, 3.0),
                    band (S::bell, 60.0, 9.0, 0.5),
                    band (S::lowShelf, 120.0, 9.0),
                    band (S::highShelf, 6000.0, -6.0),
                    band (S::lowCut, 80.0, 0.0, 0.7071, 96),
                    band (S::lowCut, 200.0, 0.0, 0.7071, 6),
                    band (S::highCut, 8000.0, 0.0, 0.7071, 18),
                    band (S::notch, 500.0, 0.0, 4.0),
                    band (S::bandPass, 2000.0, 0.0, 2.0),
                    band (S::allPass, 1500.0, 0.0, 1.0),
                };

                for (const auto& settings : cases)
                {
                    const auto design = dsp::FilterBandDesign::make (settings, fs);
                    const MeasuredResponse measured (design);
                    auto worst = 0.0;

                    for (auto f : logFrequencies (20.0, 0.45 * fs, 24))
                    {
                        const auto computed = design.magnitudeDb (f, fs);

                        if (computed < -80.0)
                            continue;   // below the float audio path's floor

                        worst = std::max (worst, std::abs (measured.db (f, fs) - computed));
                    }

                    expectLessThan (worst, 0.02, describe (settings, fs));
                }
            }
        }

        beginTest ("Bells and shelves are decramped: within tolerance of the analog curve up to Nyquist");
        {
            // Tolerances (dB, the worst point from 10 Hz to Nyquist), from the
            // method's measured worst case in each class: tight where guitars
            // live, honest about extreme settings near Nyquist. The error is
            // zero at DC, the centre and Nyquist (checked below) and largest
            // well above the centre.
            struct Tolerance { double maxGain, minQ, maxBellQ, maxShelfQ, belowRatio, db; };
            const Tolerance tolerances[] = {
                { 15.0, 0.5, 4.0, 1.0, 1.0 / 16.0, 0.45 },  // up to fs/16 (3 kHz at 48 kHz)
                { 15.0, 0.5, 4.0, 1.0, 1.0 / 8.0,  0.75 },  // up to fs/8
                { 15.0, 0.5, 4.0, 1.0, 1.0 / 4.0,  0.9 },   // up to fs/4
                { 15.0, 0.3, 4.0, 2.0, 1.0 / 4.0,  1.8 },   // very wide bells, resonant shelves
                { 30.0, 0.3, 4.0, 2.0, 1.0 / 4.0,  2.5 },   // extreme gain
                { 30.0, 0.3, 4.0, 2.0, 0.45,       3.5 },   // extreme gain right up at the top
            };

            for (auto fs : rates)
            {
                for (auto shape : { S::bell, S::lowShelf, S::highShelf })
                {
                    for (auto fc : { 30.0, 100.0, 300.0, 1000.0, 3000.0, 6000.0, 10000.0, 15000.0, 20000.0 })
                    {
                        for (auto gain : { -30.0, -15.0, -6.0, -1.0, 1.0, 6.0, 15.0, 30.0 })
                        {
                            for (auto q : { 0.3, 0.5, 0.7071, 1.0, 2.0, 4.0 })
                            {
                                if (shape != S::bell && q > 2.0)
                                    continue;   // resonant shelves beyond Q 2 aren't used

                                const Tolerance* tolerance = nullptr;

                                for (const auto& t : tolerances)
                                    if (std::abs (gain) <= t.maxGain && q >= t.minQ && fc <= t.belowRatio * fs
                                        && q <= (shape == S::bell ? t.maxBellQ : t.maxShelfQ))
                                        { tolerance = &t; break; }

                                if (tolerance == nullptr)
                                    continue;

                                const auto settings = band (shape, fc, gain, q);
                                const auto design = dsp::FilterBandDesign::make (settings, fs);
                                expect (design.sections[0].isStable(), describe (settings, fs));

                                auto worst = 0.0;

                                for (auto f : logFrequencies (10.0, 0.4999 * fs, 200))
                                {
                                    const auto reference = shape == S::bell     ? dsp::analog::bellDb (f, fc, q, gain)
                                                         : shape == S::lowShelf ? dsp::analog::lowShelfDb (f, fc, q, gain)
                                                                                : dsp::analog::highShelfDb (f, fc, q, gain);
                                    worst = std::max (worst, std::abs (design.magnitudeDb (f, fs) - reference));
                                }

                                expectLessThan (worst, tolerance->db, describe (settings, fs));

                                // Exact (to rounding) at the three matched points.
                                const auto at = [&] (double f) { return design.magnitudeDb (f, fs); };
                                const auto ref = [&] (double f)
                                {
                                    return shape == S::bell     ? dsp::analog::bellDb (f, fc, q, gain)
                                         : shape == S::lowShelf ? dsp::analog::lowShelfDb (f, fc, q, gain)
                                                                : dsp::analog::highShelfDb (f, fc, q, gain);
                                };
                                expectWithinAbsoluteError (at (fc), ref (fc), 0.01, describe (settings, fs));
                                expectWithinAbsoluteError (at (0.5 * fs), ref (0.5 * fs), 0.01, describe (settings, fs));
                            }
                        }
                    }
                }
            }
        }

        beginTest ("Decramping matters: a 12 kHz bell at 44.1 kHz matches the analog curve, the bilinear one doesn't");
        {
            const auto fs = 44100.0;
            const auto matched = dsp::design::matchedBell (fs, 12000.0, 1.0, 12.0);
            const auto analogAt16k = dsp::analog::bellDb (16000.0, 12000.0, 1.0, 12.0);
            expectWithinAbsoluteError (matched.magnitudeDb (16000.0, fs), analogAt16k, 0.8);

            // The plain bilinear bell (prewarped at the centre) squeezes towards Nyquist.
            const auto g = std::tan (juce::MathConstants<double>::pi * 12000.0 / fs);
            const auto A = std::pow (10.0, 12.0 / 40.0);
            dsp::BiquadDesign cramped;
            {
                const auto k = 1.0 / (1.0 * A);
                const auto d = 1.0 + k * g + g * g;
                const auto n1 = k * A * A;   // s^2 + s A/Q + 1 over s^2 + s/(A Q) + 1
                cramped.b0 = (1.0 + n1 * g + g * g) / d;
                cramped.b1 = 2.0 * (g * g - 1.0) / d;
                cramped.b2 = (1.0 - n1 * g + g * g) / d;
                cramped.a1 = 2.0 * (g * g - 1.0) / d;
                cramped.a2 = (1.0 - k * g + g * g) / d;
            }
            expectGreaterThan (std::abs (cramped.magnitudeDb (16000.0, fs) - analogAt16k), 2.0);
        }

        beginTest ("Cuts: Butterworth from 6 to 96 dB/oct, exact to the maths, -3 dB at the corner");
        {
            for (auto fs : rates)
            {
                for (int slope = 6; slope <= 96; slope += 6)
                {
                    const auto order = slope / 6;

                    for (auto shape : { S::lowCut, S::highCut })
                    {
                        const auto fc = shape == S::lowCut ? 100.0 : 5000.0;
                        const auto settings = band (shape, fc, 0.0, 0.7071, slope);
                        const auto design = dsp::FilterBandDesign::make (settings, fs);
                        expectEquals (design.numSections, (order + 1) / 2, describe (settings, fs));

                        // Bilinear Butterworth: |H|^2 = 1 / (1 + (tan(pi f/fs) / tan(pi fc/fs))^(2n)).
                        const auto warp = [fs] (double f) { return std::tan (juce::MathConstants<double>::pi * f / fs); };
                        auto worst = 0.0;

                        for (auto f : logFrequencies (10.0, 0.49 * fs, 60))
                        {
                            const auto ratio = shape == S::lowCut ? warp (fc) / warp (f) : warp (f) / warp (fc);
                            const auto exact = -10.0 * std::log10 (1.0 + std::pow (ratio, 2.0 * order));

                            if (exact > -200.0)
                                worst = std::max (worst, std::abs (design.magnitudeDb (f, fs) - exact));
                        }

                        expectLessThan (worst, 0.001, describe (settings, fs));
                        expectWithinAbsoluteError (design.magnitudeDb (fc, fs), -3.0103, 0.001);
                    }

                    // The low cut's slope in the stop band is the slope asked for
                    // ("6 dB/oct" per order is exactly 20 log10(2) = 6.02 dB).
                    const auto lowCut = dsp::FilterBandDesign::make (band (S::lowCut, 800.0, 0.0, 0.7071, slope), fs);
                    const auto perOctave = lowCut.magnitudeDb (50.0, fs) - lowCut.magnitudeDb (25.0, fs);
                    expectWithinAbsoluteError (perOctave, order * 20.0 * std::log10 (2.0), 0.02);   // 5 octaves below: first order is still 0.01 dB short
                }
            }
        }

        beginTest ("A fast sweep stays stable and bounded");
        {
            for (auto fs : rates)
            {
                for (const auto& shape : { band (S::bell, 0.0, 18.0, 8.0), band (S::lowCut, 0.0, 0.0, 0.7071, 96),
                                           band (S::highShelf, 0.0, -24.0, 1.0), band (S::highCut, 0.0, 0.0, 0.7071, 48) })
                {
                    for (int updateEvery : { 1, 16 })
                    {
                        dsp::FilterBand<1> filter;
                        auto signal = noise (0.5f, (int) (0.4 * fs), 7);
                        auto settings = shape;
                        auto peakOut = 0.0f;
                        auto allFinite = true;

                        // 20 Hz -> 20 kHz -> 20 Hz in 0.4 s (a pedal slammed twice), every update.
                        for (size_t i = 0; i < signal.size(); ++i)
                        {
                            if (i % (size_t) updateEvery == 0)
                            {
                                const auto phase = (double) i / (double) signal.size();
                                const auto sweep = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
                                settings.frequency = 20.0 * std::pow (1000.0, sweep);
                                filter.setDesign (dsp::FilterBandDesign::make (settings, fs));
                            }

                            const auto y = filter.processSample (signal[i], 0);
                            allFinite = allFinite && std::isfinite (y);
                            peakOut = std::max (peakOut, std::abs (y));
                        }

                        expect (allFinite, describe (shape, fs));
                        expectLessThan (peakOut, 0.5f * 8.0f * 10.0f, describe (shape, fs) + " (bounded)");
                    }
                }
            }
        }

        beginTest ("Silence in, silence out (every shape, from rest and after loud input)");
        {
            for (auto fs : rates)
            {
                for (auto shape : { S::bell, S::lowShelf, S::highShelf, S::lowCut, S::highCut, S::notch, S::bandPass, S::allPass })
                {
                    dsp::FilterBand<2> filter;
                    filter.setDesign (dsp::FilterBandDesign::make (band (shape, 1000.0, 12.0, 2.0, 48), fs));

                    std::vector<float> left (4096, 0.0f), right (4096, 0.0f);
                    float* channels[2] = { left.data(), right.data() };
                    filter.process (channels, 2, 4096);
                    expect (peak (left, 0, 4096) == 0.0f && peak (right, 0, 4096) == 0.0f, "from rest");

                    // Loud input, then a long silence: the tail decays (no limit cycle, no NaN).
                    const juce::ScopedNoDenormals noDenormals;
                    auto loud = noise (1.0f, (int) fs, 3);
                    float* loudChannels[1] = { loud.data() };
                    filter.process (loudChannels, 1, (int) loud.size());
                    std::vector<float> tail ((size_t) fs * 2, 0.0f);
                    float* tailChannels[1] = { tail.data() };
                    filter.process (tailChannels, 1, (int) tail.size());
                    expectLessThan (peak (tail, (int) tail.size() - 1024, (int) tail.size()), 1.0e-6f);
                }
            }
        }

        beginTest ("No NaN or infinity at extreme settings");
        {
            for (auto fs : rates)
            {
                for (auto shape : { S::bell, S::lowShelf, S::highShelf, S::lowCut, S::highCut, S::notch, S::bandPass, S::allPass })
                {
                    for (auto frequency : { 0.0, 1.0e-3, 5.0, 0.5 * fs, 1.0e9 })
                    {
                        for (auto gain : { -1000.0, -60.0, 60.0, 1000.0 })
                        {
                            for (auto q : { 0.0, 1.0e-4, 100.0, 1.0e6 })
                            {
                                for (auto slope : { 0, 6, 96, 500 })
                                {
                                    const auto settings = band (shape, frequency, gain, q, slope);
                                    const auto design = dsp::FilterBandDesign::make (settings, fs);
                                    dsp::FilterBand<1> filter;
                                    filter.setDesign (design);
                                    auto allFinite = std::isfinite (design.magnitudeDb (1000.0, fs));

                                    for (int i = 0; i < 2048; ++i)
                                        allFinite = allFinite && std::isfinite (filter.processSample (i % 64 == 0 ? 1.0f : 0.0f, 0));

                                    expect (allFinite, describe (settings, fs));
                                }
                            }
                        }
                    }
                }
            }
        }

        beginTest ("No allocation on the audio thread, designing included");
        {
            dsp::FilterBand<2> filter;
            std::vector<float> a ((size_t) blockSize, 0.25f), b ((size_t) blockSize, -0.25f);
            float* channels[2] = { a.data(), b.data() };

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 64; ++i)
                {
                    const auto shape = (S) (i % 8);
                    filter.setDesign (dsp::FilterBandDesign::make (band (shape, 100.0 + i * 50.0, 6.0, 1.0, 6 * (1 + i % 16)), sampleRate));
                    filter.process (channels, 2, blockSize);
                    const dsp::FilterBandDesign designs[] = { filter.getDesign(), filter.getDesign() };
                    juce::ignoreUnused (dsp::responseDb (designs, 1000.0, sampleRate));
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }

        beginTest ("The response of a set of bands is the sum of its bands");
        {
            const dsp::FilterBandDesign designs[] = {
                dsp::FilterBandDesign::make (band (S::lowCut, 90.0, 0.0, 0.7071, 24), sampleRate),
                dsp::FilterBandDesign::make (band (S::bell, 300.0, -3.0, 1.2), sampleRate),
                dsp::FilterBandDesign::make (band (S::highShelf, 4000.0, 2.0), sampleRate),
            };

            for (auto f : { 50.0, 300.0, 1000.0, 8000.0 })
                expectWithinAbsoluteError (dsp::responseDb (designs, f, sampleRate),
                                           designs[0].magnitudeDb (f, sampleRate) + designs[1].magnitudeDb (f, sampleRate)
                                               + designs[2].magnitudeDb (f, sampleRate), 1.0e-9);

            expectEquals (dsp::responseDb ({}, 1000.0, sampleRate), 0.0);
        }

        beginTest ("A glide lands exactly on its design; a section can glide in from and out to nothing");
        {
            const auto bell = dsp::FilterBandDesign::make (band (S::bell, 800.0, 9.0, 1.5), sampleRate);
            const auto none = dsp::FilterBandDesign::make (band (S::bell, 800.0, 0.0, 1.5), sampleRate);

            // Glided over 16 samples, then held: the same as set at once, after the glide.
            dsp::FilterBand<1> glided, direct;
            glided.setDesign (none);
            glided.glideTo (bell, 16);
            expect (glided.isGliding());
            direct.setDesign (bell);
            auto a = noise (0.5f, 4096, 12), b = a;
            float* ca[1] = { a.data() };
            float* cb[1] = { b.data() };
            glided.process (ca, 1, 4096);
            direct.process (cb, 1, 4096);
            expect (! glided.isGliding());

            auto worst = 0.0f;

            for (size_t i = 2048; i < a.size(); ++i)
                worst = std::max (worst, std::abs (a[i] - b[i]));

            expectLessThan (worst, 1.0e-6f, "settles to the same filter");

            // Gliding out to nothing: afterwards it is bit-exact passthrough again.
            glided.glideTo (none, 16);
            std::vector<float> skip (64, 0.1f);
            float* cs[1] = { skip.data() };
            glided.process (cs, 1, 64);
            const auto input = noise (0.5f, 1024, 13);
            auto output = input;
            float* co[1] = { output.data() };
            glided.process (co, 1, 1024);
            expect (output == input);

            // No allocation while gliding.
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 64; ++i)
                {
                    glided.glideTo (dsp::FilterBandDesign::make (band (S::bell, 800.0, (i % 2) ? 9.0 : 0.0, 1.5), sampleRate), 16);
                    glided.process (co, 1, 16);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }

        beginTest ("A band at 0 dB changes nothing (bit-exact)");
        {
            dsp::FilterBand<1> filter;
            filter.setDesign (dsp::FilterBandDesign::make (band (S::bell, 1000.0, 0.0, 1.0), sampleRate));
            const auto input = noise (0.5f, 4800);
            auto output = input;
            float* channels[1] = { output.data() };
            filter.process (channels, 1, (int) output.size());
            expect (output == input);
        }
    }
};

class DetectorTests final : public juce::UnitTest
{
public:
    DetectorTests() : juce::UnitTest ("Module detectors", "Encore") {}

    struct GateRun
    {
        std::vector<float> gains;
        int opens = 0, closes = 0;
        int firstFullyOpen = -1;
    };

    static GateRun runGate (const std::vector<float>& signal, dsp::DetectorMode mode, double detectorMs,
                            const dsp::GateEnvelopeSettings& settings, bool startOpen, double rate = sampleRate,
                            int countFrom = 0)
    {
        dsp::LevelDetector detector;
        detector.prepare (rate);
        detector.setMode (mode);
        detector.setTimeMs (detectorMs);

        dsp::GateEnvelope gate;
        gate.prepare (rate);
        gate.setSettings (settings);
        gate.reset (startOpen);

        GateRun run;
        auto wasOpen = gate.isOpen();

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto g = gate.process (detector.process (signal[i]));
            run.gains.push_back (g);

            if (gate.isOpen() != wasOpen && (int) i >= countFrom)
                (gate.isOpen() ? run.opens : run.closes)++;

            wasOpen = gate.isOpen();

            if (run.firstFullyOpen < 0 && g >= 1.0f)
                run.firstFullyOpen = (int) i;
        }

        return run;
    }

    void runTest() override
    {
        beginTest ("Peak reads a sine's peak; RMS reads it 3 dB lower, within 0.2 dB");
        {
            for (auto rate : rates)
            {
                for (auto amplitudeDb : { -40.0f, -12.0f, 0.0f })
                {
                    const auto amplitude = juce::Decibels::decibelsToGain (amplitudeDb);
                    std::vector<float> tone ((size_t) rate);

                    for (size_t i = 0; i < tone.size(); ++i)
                        tone[i] = amplitude * (float) std::sin (twoPi * 440.0 * (double) i / rate);

                    dsp::LevelDetector peakDetector, rmsDetector;
                    peakDetector.prepare (rate);
                    rmsDetector.prepare (rate);
                    rmsDetector.setMode (dsp::DetectorMode::rms);
                    rmsDetector.setTimeMs (50.0);

                    float peakMax = 0.0f, rmsLast = 0.0f;

                    for (auto x : tone)
                    {
                        peakMax = std::max (peakMax, peakDetector.process (x));
                        rmsLast = rmsDetector.process (x);
                    }

                    expectWithinAbsoluteError (toDb (peakMax), amplitudeDb, 0.01f);
                    expectWithinAbsoluteError (toDb (rmsLast), amplitudeDb - 3.0103f, 0.2f);
                }
            }
        }

        beginTest ("RMS follows loudness: within 1 dB once settled, and after a 12 dB step");
        {
            dsp::LevelDetector detector;
            detector.prepare (sampleRate);
            detector.setMode (dsp::DetectorMode::rms);
            detector.setTimeMs (50.0);

            // Noise, 12 dB louder after one second.
            auto signal = whiteNoise (0.25f, 96000, 4);

            for (size_t i = 48000; i < signal.size(); ++i)
                signal[i] *= 4.0f;

            std::vector<float> level;

            for (auto x : signal)
                level.push_back (detector.process (x));

            const auto quiet = rms (signal, 24000, 48000);
            const auto loud = rms (signal, 72000, 96000);

            // Settled readings track the true RMS within 1 dB (noise ripple included).
            auto worst = 0.0f;

            for (int i = 30000; i < 48000; i += 480)
                worst = std::max (worst, std::abs (toDb (level[(size_t) i]) - toDb (quiet)));

            for (int i = 78000; i < 96000; i += 480)
                worst = std::max (worst, std::abs (toDb (level[(size_t) i]) - toDb (loud)));

            expectLessThan (worst, 1.0f);

            // Five time constants (250 ms) after the step it reads the new level within 1 dB.
            expectWithinAbsoluteError (toDb (level[48000 + 12000]), toDb (loud), 1.0f);
        }

        beginTest ("A pluck opens the gate within the attack, at every rate");
        {
            for (auto rate : rates)
            {
                for (auto attackMs : { 0.05f, 0.5f, 5.0f, 50.0f })
                {
                    const auto note = pluck ((int) rate, (int) (0.1 * rate), 0.5f, 2000.0, -70.0f, 110.0, 6, 2, rate);

                    dsp::GateEnvelopeSettings settings;
                    settings.thresholdDb = -50.0f;
                    settings.attackMs = attackMs;

                    const auto run = runGate (note.signal, dsp::DetectorMode::peak, 10.0, settings, false, rate);
                    expect (run.firstFullyOpen > note.onset, "closed on the noise floor before the note");

                    // The first sample at or above the threshold.
                    auto crossing = -1;

                    for (size_t i = 0; i < note.signal.size() && crossing < 0; ++i)
                        if (toDb (std::abs (note.signal[i])) >= settings.thresholdDb)
                            crossing = (int) i;

                    const auto attackSamples = (int) std::ceil (attackMs * 0.001 * rate);
                    expectLessOrEqual (run.firstFullyOpen - crossing, attackSamples,
                                       juce::String (attackMs) + " ms at " + juce::String (rate));
                    expectEquals (run.gains[(size_t) note.onset - 1], 0.0f, "fully closed before (range -90 dB = silence)");
                }
            }
        }

        beginTest ("Hysteresis: a slowly decaying sine closes exactly once, no chatter");
        {
            // 0 dBFS falling 20 dB per second through -40 dB, with a little noise riding on it.
            auto signal = decayingSine (196.0, 1.0f, 20.0, (int) (4.0 * sampleRate));
            const auto hiss = noise (juce::Decibels::decibelsToGain (-50.0f), (int) signal.size(), 9);

            for (size_t i = 0; i < signal.size(); ++i)
                signal[i] += hiss[i];

            dsp::GateEnvelopeSettings settings;
            settings.thresholdDb = -40.0f;
            settings.hysteresisDb = 6.0f;
            settings.holdMs = 0.0f;
            const auto afterStart = (int) (0.1 * sampleRate);   // the sine starts at zero: ignore the first 100 ms

            for (auto mode : { dsp::DetectorMode::peak, dsp::DetectorMode::rms })
            {
                const auto run = runGate (signal, mode, 10.0, settings, true, sampleRate, afterStart);
                expectEquals (run.closes, 1, "closes once");
                expectEquals (run.opens, 0, "never reopens");
            }

            // Without hysteresis and with a fast detector the same note chatters:
            // the test can see what hysteresis prevents.
            settings.hysteresisDb = 0.0f;
            const auto chatter = runGate (signal, dsp::DetectorMode::peak, 1.0, settings, true, sampleRate, afterStart);
            expectGreaterThan (chatter.closes, 1);
        }

        beginTest ("Hold keeps it open; release reaches the range in the release time, linearly in dB");
        {
            dsp::GateEnvelopeSettings settings;
            settings.thresholdDb = -40.0f;
            settings.hysteresisDb = 0.0f;
            settings.holdMs = 20.0f;
            settings.releaseMs = 100.0f;
            settings.rangeDb = -30.0f;

            dsp::GateEnvelope gate;
            gate.prepare (sampleRate);
            gate.setSettings (settings);
            gate.reset (true);

            // The level drops to silence at sample 0.
            std::vector<float> gains;

            for (int i = 0; i < 9600; ++i)
                gains.push_back (gate.process (0.0f));

            const auto holdSamples = 960;
            expectEquals (gains[(size_t) holdSamples - 1], 1.0f, "still open through the hold");
            expect (gains[(size_t) holdSamples + 1] < 1.0f, "then releases");

            const auto releaseEnd = holdSamples + 1 + 4800;
            expectWithinAbsoluteError (toDb (gains[(size_t) releaseEnd - 1]), -30.0f, 0.1f);
            expectWithinAbsoluteError (toDb (gains[(size_t) holdSamples + 1 + 2400]), -15.0f, 0.1f, "half way in dB at half time");
            expectEquals (gains.back(), juce::Decibels::decibelsToGain (-30.0f), "settles at the range");
        }

        beginTest ("A palm-mute opens and closes; the noise floor alone never opens it");
        {
            const auto mute = palmMute (48000, 4800, 0.5f, -70.0f, 5);
            dsp::GateEnvelopeSettings settings;
            settings.thresholdDb = -55.0f;

            const auto run = runGate (mute.signal, dsp::DetectorMode::peak, 10.0, settings, false);
            expectEquals (run.opens, 1);
            expectEquals (run.closes, 1);

            const auto floorOnly = runGate (noise (juce::Decibels::decibelsToGain (-70.0f), 48000, 6),
                                            dsp::DetectorMode::peak, 10.0, settings, false);
            expectEquals (floorOnly.opens, 0);
        }

        beginTest ("No allocation, no NaN at extreme settings");
        {
            dsp::LevelDetector detector;
            dsp::GateEnvelope gate;
            detector.prepare (sampleRate);
            gate.prepare (sampleRate);
            auto allFinite = true;

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (auto attack : { 0.0f, 0.05f, 1.0e6f })
                    for (auto release : { 0.0f, 5.0f, 1.0e6f })
                        for (auto range : { -1000.0f, -90.0f, 0.0f, 20.0f })
                            for (auto time : { 0.0, 1.0, 1.0e9 })
                            {
                                detector.setTimeMs (time);
                                gate.setSettings ({ -60.0f, 100.0f, range, attack, 1.0e6f, release });

                                for (int i = 0; i < 256; ++i)
                                    allFinite = allFinite && std::isfinite (gate.process (detector.process (i % 32 == 0 ? 1.0f : 0.0f)));
                            }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expect (allFinite);
        }
    }
};

class TestSignalTests final : public juce::UnitTest
{
public:
    TestSignalTests() : juce::UnitTest ("Module test signals", "Encore") {}

    void runTest() override
    {
        beginTest ("Deterministic: the same seed gives the same samples");
        {
            expect (pinkNoise (0.5f, 4096, 3) == pinkNoise (0.5f, 4096, 3));
            expect (pinkNoise (0.5f, 4096, 3) != pinkNoise (0.5f, 4096, 4));
            expect (pluck (9600, 100, 0.5f, 500.0, -70.0f, 110.0, 6, 8).signal == pluck (9600, 100, 0.5f, 500.0, -70.0f, 110.0, 6, 8).signal);
            expect (logSweep (20.0, 20000.0, 0.5f, 48000) == logSweep (20.0, 20000.0, 0.5f, 48000));
        }

        beginTest ("Pink noise has equal power per octave; white rises 3 dB per octave");
        {
            const auto bandPower = [] (const std::vector<float>& signal, double centre)
            {
                dsp::FilterBand<1> band;
                band.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::bandPass, centre, 0.0, 1.41 }, sampleRate));
                auto filtered = signal;
                float* channels[1] = { filtered.data() };
                band.process (channels, 1, (int) filtered.size());
                return toDb (rms (filtered, 4800, (int) filtered.size()));
            };

            const auto pink = pinkNoise (0.5f, 480000, 11);
            const auto white = whiteNoise (0.5f, 480000, 11);
            expectWithinAbsoluteError (bandPower (pink, 2000.0) - bandPower (pink, 250.0), 0.0f, 1.5f);
            expectWithinAbsoluteError (bandPower (white, 2000.0) - bandPower (white, 250.0), 9.03f, 1.0f);
        }

        beginTest ("A pluck: the stated envelope over the stated floor");
        {
            const auto note = pluck (96000, 4800, 0.5f, 1000.0, -70.0f);
            expectLessOrEqual (peak (note.signal, 0, note.onset), note.noiseFloor);
            expectWithinAbsoluteError (note.envelope[(size_t) note.onset + 95], 0.5f, 1.0e-6f, "peak at the end of the 2 ms rise");
            expectWithinAbsoluteError (toDb (note.envelope[(size_t) note.onset + 96 + 48000]), toDb (0.5f) - 60.0f, 0.1f, "60 dB down after the decay time");
            expectLessOrEqual (peak (note.signal, note.onset, (int) note.signal.size()), 0.5f + note.noiseFloor);
        }

        beginTest ("Impulse and sweep");
        {
            const auto i = impulse (64, 10, 0.5f);
            expectEquals (i[10], 0.5f);
            expectEquals (peak (i, 0, 10), 0.0f);

            const auto sweep = logSweep (20.0, 20000.0, 0.5f, 48000);
            expectLessOrEqual (peak (sweep, 0, 48000), 0.5f);
            expectGreaterThan (peak (sweep, 0, 48000), 0.49f);
        }
    }
};

class OversamplerTests final : public juce::UnitTest
{
public:
    OversamplerTests() : juce::UnitTest ("Module oversampling", "Encore") {}

    static constexpr int fftOrder = 15;
    static constexpr int fftSize = 1 << fftOrder;

    /** Drives a sine (2.5 kHz: bin 1707 of 32768 at 48 kHz) into tanh as hard
        as a fuzz (x100) and returns the aliasing: the power in every bin that
        is not a real harmonic, relative to the total, in dB. The sine sits
        exactly on a bin, so its harmonics and their aliases land on distinct
        bins. quality -1 = no oversampling. (A tone whose harmonics land on
        multiples of the rate, like 3 kHz at 48 kHz, would hide its aliases.) */
    static double aliasingDb (int qualityIndex, int bin = 1707, float driveGain = 100.0f)
    {
        const auto frequency = bin * sampleRate / fftSize;
        std::vector<float> signal ((size_t) fftSize * 2);

        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = 0.5f * (float) std::sin (twoPi * frequency * (double) i / sampleRate);

        const auto drive = [driveGain] (float* const* channels, int numChannels, int numSamples, double, int)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    channels[ch][i] = std::tanh (driveGain * channels[ch][i]);
        };

        if (qualityIndex < 0)
        {
            float* channels[1] = { signal.data() };
            drive (channels, 1, (int) signal.size(), sampleRate, 0);
        }
        else
        {
            dsp::Oversampler os;
            os.setQuality ((dsp::Quality) qualityIndex);
            os.prepare (1, blockSize, sampleRate);

            for (size_t offset = 0; offset < signal.size(); offset += (size_t) blockSize)
            {
                float* channels[1] = { signal.data() + offset };
                os.process (channels, 1, blockSize, drive);
            }
        }

        // The second half, settled.
        std::vector<float> fft ((size_t) fftSize * 2, 0.0f);
        std::copy (signal.begin() + fftSize, signal.end(), fft.begin());
        juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (fft.data());

        double total = 0.0, harmonics = 0.0;

        for (int b = 1; b < fftSize / 2; ++b)
        {
            const auto power = (double) fft[(size_t) b] * fft[(size_t) b];
            total += power;

            if (b % bin == 0)
                harmonics += power;
        }

        return 10.0 * std::log10 (std::max (total - harmonics, 1.0e-30) / total);
    }

    void runTest() override
    {
        beginTest ("A tanh stage's aliasing drops with the factor");
        {
            const auto none = aliasingDb (-1);
            const auto live = aliasingDb (0);
            const auto high = aliasingDb (1);
            const auto max = aliasingDb (2);
            logMessage ("Aliasing (dB below the signal): none " + juce::String (none, 1) + ", Live 2x " + juce::String (live, 1)
                        + ", High 4x " + juce::String (high, 1) + ", Max 8x " + juce::String (max, 1));

            expectLessThan (live, none - 6.0);
            expectLessThan (high, live - 6.0);
            expectLessThan (max, high - 6.0);
        }

        beginTest ("Factors and latency per Quality; linear content passes unchanged in level");
        {
            for (auto rate : rates)
            {
                dsp::Oversampler os;
                os.prepare (2, blockSize, rate);

                for (int q = 0; q < dsp::Oversampler::numQualities; ++q)
                {
                    os.setQuality ((dsp::Quality) q);
                    auto tone = sine (1000.0 * rate / sampleRate, 0.5f, (int) rate / 2);
                    auto seenFactor = 0;
                    auto seenRate = 0.0;

                    for (size_t offset = 0; offset + (size_t) blockSize <= tone.size(); offset += (size_t) blockSize)
                    {
                        float* channels[1] = { tone.data() + offset };
                        os.process (channels, 1, blockSize, [&] (float* const*, int, int n, double upRate, int)
                        {
                            seenFactor = n / blockSize;
                            seenRate = upRate;
                        });
                    }

                    expectEquals (seenFactor, dsp::Oversampler::factorFor ((dsp::Quality) q));
                    expectEquals (seenRate, rate * seenFactor);
                    expectEquals (os.getFactor(), seenFactor);
                    expect (os.getLatencySamples() >= 0 && os.getLatencySamples() < 64, juce::String (os.getLatencySamples()));
                    expectWithinAbsoluteError (toDb (peak (tone, (int) tone.size() / 2, (int) tone.size())), toDb (0.5f), 0.1f);
                }
            }
        }

        beginTest ("Switching Quality allocates nothing and crossfades without a jump");
        {
            dsp::Oversampler os;
            os.prepare (2, blockSize, sampleRate);

            const auto input = sine (220.0, 0.5f, 48000);
            auto maxInputStep = 0.0f;

            for (size_t i = 1; i < input.size(); ++i)
                maxInputStep = std::max (maxInputStep, std::abs (input[i] - input[i - 1]));

            auto left = input, right = input;
            const auto softClip = [] (float* const* channels, int numChannels, int numSamples, double, int)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int i = 0; i < numSamples; ++i)
                        channels[ch][i] = std::tanh (channels[ch][i]);
            };

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int b = 0; b + blockSize <= (int) input.size(); b += blockSize)
                {
                    if (b % (blockSize * 20) == 0)
                        os.setQuality ((dsp::Quality) ((b / (blockSize * 20)) % 3));

                    float* channels[2] = { left.data() + b, right.data() + b };
                    os.process (channels, 2, blockSize, softClip);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);

            auto maxStep = 0.0f;

            for (size_t i = 1; i < left.size(); ++i)
                maxStep = std::max (maxStep, std::abs (left[i] - left[i - 1]));

            expectLessThan (maxStep, maxInputStep * 1.5f);
            expect (left == right);
        }

        beginTest ("Not prepared: passes through, no crash");
        {
            dsp::Oversampler os;
            auto signal = noise (0.5f, 256);
            const auto original = signal;
            float* channels[1] = { signal.data() };
            os.process (channels, 1, 256, [] (float* const*, int, int, double, int) {});
            expect (signal == original);
        }
    }
};

static FilterTests filterTests;
static OversamplerTests oversamplerTests;
static DetectorTests detectorTests;
static TestSignalTests testSignalTests;

} // namespace encore::test
