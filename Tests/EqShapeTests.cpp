// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3 step 1: every filter shape at every slope (the
// Studio EQ's bands), against its analog prototype at every rate Encore
// runs at. The slope is the filter's order x 6 dB/oct, as in Pro-Q.

#include "TestSignals.h"

#include "engine/dsp/FilterBand.h"

#include <complex>

namespace encore::test
{

namespace
{
    using S = dsp::FilterShape;
    constexpr double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr int slopes[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    dsp::FilterSettings band (S shape, double f, double gainDb, double q, int slope)
    {
        dsp::FilterSettings s;
        s.shape = shape;
        s.frequency = f;
        s.gainDb = gainDb;
        s.q = q;
        s.slopeDbPerOctave = slope;
        return s;
    }

    juce::String describe (const dsp::FilterSettings& s, double fs)
    {
        return "shape " + juce::String ((int) s.shape) + " " + juce::String (s.frequency) + " Hz "
               + juce::String (s.gainDb) + " dB Q " + juce::String (s.q) + " " + juce::String (s.slopeDbPerOctave)
               + " dB/oct at " + juce::String (fs);
    }

    std::complex<double> response (const dsp::FilterBandDesign& d, double f, double fs)
    {
        std::complex<double> h (1.0, 0.0);

        for (int s = 0; s < d.numSections; ++s)
            h *= d.sections[(size_t) s].responseAt (twoPi * f / fs);

        return h;
    }

    /** The analog prototype of a shelf at a slope: today's second-order shelf
        at 12 dB/oct (Q shapes the corner), else a Butterworth shelf of the order. */
    double shelfPrototypeDb (S shape, double f, double fc, double q, double g, int slope)
    {
        const auto order = slope / 6;
        const auto high = order == 2 ? dsp::analog::highShelfDb (f, fc, q, g) : dsp::analog::butterworthHighShelfDb (f, fc, order, g);

        if (shape == S::lowShelf)
            return order == 2 ? dsp::analog::lowShelfDb (f, fc, q, g) : dsp::analog::butterworthLowShelfDb (f, fc, order, g);

        return shape == S::tiltShelf ? high - g / 2.0 : high;
    }

    /** An impulse through the real audio path; its DFT at a frequency. */
    double measuredDb (const dsp::FilterBandDesign& design, double f, double fs, int length = 1 << 16)
    {
        dsp::FilterBand<1> playing;
        playing.setDesign (design);
        std::vector<float> x ((size_t) length, 0.0f);
        x[0] = 1.0f;
        float* channels[] = { x.data() };
        playing.process (channels, 1, length);

        std::complex<double> sum, z (1.0, 0.0);
        const auto step = std::polar (1.0, -twoPi * f / fs);

        for (auto v : x)
        {
            sum += (double) v * z;
            z *= step;
        }

        return 20.0 * std::log10 (std::max (std::abs (sum), 1.0e-15));
    }
}

class EqShapeTests final : public juce::UnitTest
{
public:
    EqShapeTests() : juce::UnitTest ("EQ shapes and slopes", "Encore") {}

    void runTest() override
    {
        beginTest ("Shelves and tilt at every slope: decramped, against the analog prototype at every rate");
        {
            // Measured envelopes, graded by how high the corner sits, as B0's
            // shelf test does (the dsp README states them). A first-order
            // shelf can't follow a +30 dB prototype whose transition runs past
            // Nyquist; steep shelves set high lose a little at the corner,
            // where their high-Q sections are matched at 3 points each.
            double worstLow = 0.0, worstFirstOrder = 0.0, worstMid = 0.0, worstAll = 0.0;

            for (auto shape : { S::lowShelf, S::highShelf, S::tiltShelf })
                for (auto fs : rates)
                    for (auto slope : slopes)
                        for (auto fc : { 30.0, 100.0, 1000.0, 3000.0, 8000.0, 16000.0 })
                            for (auto g : { -30.0, -12.0, -3.0, 6.0, 12.0, 30.0 })
                            {
                                if (fc > 0.45 * fs)
                                    continue;

                                const auto settings = band (shape, fc, g, 0.7071, slope);
                                const auto d = dsp::FilterBandDesign::make (settings, fs);

                                for (int s = 0; s < d.numSections; ++s)
                                    expect (d.sections[(size_t) s].isStable(), describe (settings, fs));

                                for (double f = 10.0; f < 0.45 * fs; f *= 1.03)
                                {
                                    const auto err = std::abs (d.magnitudeDb (f, fs) - shelfPrototypeDb (shape, f, fc, 0.7071, g, slope));
                                    const auto belowQuarter = f < fs / 4.0;

                                    if (fc <= fs / 48.0 && belowQuarter)
                                        (slope == 6 ? worstFirstOrder : worstLow) = std::max (slope == 6 ? worstFirstOrder : worstLow, err);
                                    else if (fc <= fs / 10.0 && std::abs (g) <= 12.0 && belowQuarter)
                                        worstMid = std::max (worstMid, err);

                                    worstAll = std::max (worstAll, err);
                                }
                            }

            logMessage ("Shelves and tilt, worst error against the prototype: corner up to fs/48 " + juce::String (worstLow, 3)
                        + " dB (6 dB/oct: " + juce::String (worstFirstOrder, 3) + " dB); corner up to fs/10 at +-12 dB "
                        + juce::String (worstMid, 3) + " dB; anything, to 0.9 Nyquist " + juce::String (worstAll, 3) + " dB");
            expectLessOrEqual (worstLow, 0.1, "12-96 dB/oct, corner up to 1 kHz at 48 kHz: within 0.1 dB below fs/4");
            expectLessOrEqual (worstFirstOrder, 0.15, "6 dB/oct, likewise");
            expectLessOrEqual (worstMid, 0.5, "every slope, corner up to 4.8 kHz at 48 kHz, +-12 dB");
            expectLessOrEqual (worstAll, 2.5, "anything, anywhere");
        }

        beginTest ("A tilt is -gain/2 below and +gain/2 above; Q shapes a 12 dB/oct shelf's corner, not a steeper one's");
        {
            for (auto fs : rates)
                for (auto slope : slopes)
                {
                    const auto d = dsp::FilterBandDesign::make (band (S::tiltShelf, 800.0, 10.0, 0.7071, slope), fs);
                    expectWithinAbsoluteError (d.magnitudeDb (10.0, fs), -5.0, 0.05);
                    expectWithinAbsoluteError (d.magnitudeDb (0.45 * fs, fs), 5.0, 0.1);
                    expectWithinAbsoluteError (d.magnitudeDb (800.0, fs), 0.0, 0.05, "0 dB at its frequency");
                }

            const auto bump = [] (int slope, double q)
            {
                const auto d = dsp::FilterBandDesign::make (band (S::lowShelf, 200.0, 12.0, q, slope), 48000.0);
                return d.magnitudeDb (400.0, 48000.0);
            };

            expect (std::abs (bump (12, 0.5) - bump (12, 2.0)) > 1.0, "12 dB/oct: Q moves the corner");
            expectWithinAbsoluteError (bump (24, 0.5), bump (24, 2.0), 1.0e-9, "24 dB/oct: Q has no effect");
            expectWithinAbsoluteError (bump (6, 0.5), bump (6, 2.0), 1.0e-9, "6 dB/oct: Q has no effect");
        }

        beginTest ("A bell has no slope: one section, the same at every slope");
        {
            const auto ref = dsp::FilterBandDesign::make (band (S::bell, 1000.0, 6.0, 1.4, 12), 48000.0);

            for (auto slope : slopes)
            {
                const auto d = dsp::FilterBandDesign::make (band (S::bell, 1000.0, 6.0, 1.4, slope), 48000.0);
                expectEquals (d.numSections, 1);
                expectEquals (d.sections[0].b0, ref.sections[0].b0);
                expectEquals (d.sections[0].a1, ref.sections[0].a1);
            }
        }

        beginTest ("Band pass: 0 dB at the centre, the -3 dB width Q sets at every slope, steeper skirts");
        {
            for (auto fs : rates)
            {
                const auto fc = 100.0, q = 2.0;
                // The edges of a second-order band pass of Q 2 (bilinear: warped like it).
                const auto bw = std::sqrt (1.0 + 1.0 / (4.0 * q * q));
                const auto lowEdge = fc * (bw - 1.0 / (2.0 * q)), highEdge = fc * (bw + 1.0 / (2.0 * q));
                auto lastSkirt = 1.0e9;

                for (auto slope : slopes)
                {
                    const auto d = dsp::FilterBandDesign::make (band (S::bandPass, fc, 0.0, q, slope), fs);
                    expectWithinAbsoluteError (d.magnitudeDb (fc, fs), 0.0, 0.01, describe (band (S::bandPass, fc, 0, q, slope), fs));
                    expectWithinAbsoluteError (d.magnitudeDb (lowEdge, fs), -3.01, 0.1);
                    expectWithinAbsoluteError (d.magnitudeDb (highEdge, fs), -3.01, 0.1);

                    const auto skirt = d.magnitudeDb (fc * 8.0, fs);
                    expectLessOrEqual (skirt, lastSkirt + 1.0e-9, "steeper or the same as the gentler slope");
                    lastSkirt = skirt;
                }

                // 96 dB/oct is 8 sections: each skirt falls 48 dB/oct far out.
                const auto d96 = dsp::FilterBandDesign::make (band (S::bandPass, fc, 0.0, q, 96), fs);
                expectEquals (d96.numSections, 8);

                expectWithinAbsoluteError (d96.magnitudeDb (fc * 16.0, fs) - d96.magnitudeDb (fc * 32.0, fs), 48.0, 2.0);
            }
        }

        beginTest ("Notch: deep at the frequency, wider at steeper slopes, flat far away");
        {
            for (auto fs : rates)
            {
                auto lastNear = 1.0e9;

                for (auto slope : slopes)
                {
                    const auto d = dsp::FilterBandDesign::make (band (S::notch, 2000.0, 0.0, 4.0, slope), fs);
                    expectLessThan (d.magnitudeDb (2000.0, fs), -60.0);
                    expectWithinAbsoluteError (d.magnitudeDb (40.0, fs), 0.0, 0.05);
                    const auto near = d.magnitudeDb (2000.0 * 1.15, fs);
                    expectLessOrEqual (near, lastNear + 1.0e-9, "wider (or the same) at a steeper slope");
                    lastNear = near;
                }
            }
        }

        beginTest ("All pass: 0 dB everywhere; the phase at the frequency turns 90 degrees per 6 dB/oct");
        {
            for (auto fs : rates)
                for (auto slope : slopes)
                {
                    const auto d = dsp::FilterBandDesign::make (band (S::allPass, 1500.0, 0.0, 0.7071, slope), fs);

                    for (double f = 20.0; f < 0.45 * fs; f *= 1.5)
                        expectWithinAbsoluteError (d.magnitudeDb (f, fs), 0.0, 1.0e-6);

                    const auto order = slope / 6;
                    const auto phase = std::arg (response (d, 1500.0, fs)) * 180.0 / juce::MathConstants<double>::pi;
                    const auto expected = -90.0 * order;
                    const auto diff = std::remainder (phase - expected, 360.0);
                    expectWithinAbsoluteError (diff, 0.0, 0.01, describe (band (S::allPass, 1500, 0, 0.7071, slope), fs));
                }
        }

        beginTest ("What you hear is the response function: measured = computed, every shape at every slope");
        {
            for (auto fs : { 48000.0, 192000.0 })
                for (auto shape : { S::bell, S::lowShelf, S::highShelf, S::tiltShelf, S::lowCut, S::highCut, S::notch, S::bandPass, S::allPass })
                    for (auto slope : { 6, 18, 48, 96 })
                    {
                        const auto settings = band (shape, 700.0, 9.0, 1.2, slope);
                        const auto d = dsp::FilterBandDesign::make (settings, fs);
                        auto worst = 0.0;

                        for (auto f : { 50.0, 300.0, 700.0, 1800.0, 6000.0 })
                        {
                            const auto computed = d.magnitudeDb (f, fs);

                            if (computed > -80.0)
                                worst = std::max (worst, std::abs (measuredDb (d, f, fs) - computed));
                        }

                        expectLessThan (worst, 0.02, describe (settings, fs));
                    }
        }

        beginTest ("Every shape at every slope stays stable while glideTo sweeps its whole range");
        {
            for (auto fs : { 44100.0, 192000.0 })
                for (auto shape : { S::bell, S::lowShelf, S::highShelf, S::tiltShelf, S::lowCut, S::highCut, S::notch, S::bandPass, S::allPass })
                    for (auto slope : slopes)
                    {
                        dsp::FilterBand<1> playing;
                        auto in = noise (0.5f, (int) fs / 2, 11);
                        const auto n = (int) in.size();
                        auto worst = 0.0f;
                        auto finite = true;

                        for (int offset = 0; offset < n; offset += 16)
                        {
                            const auto t = (double) offset / n;
                            const auto f = 10.0 * std::pow (0.49 * fs / 10.0, t);
                            const auto g = -30.0 + 60.0 * t;
                            const auto q = 0.025 * std::pow (40.0 / 0.025, std::fmod (t * 3.0, 1.0));
                            playing.glideTo (dsp::FilterBandDesign::make (band (shape, f, g, q, slope), fs), 16);
                            float* ch[] = { in.data() + offset };
                            playing.process (ch, 1, juce::jmin (16, n - offset));

                            for (int i = offset; i < juce::jmin (offset + 16, n); ++i)
                            {
                                finite = finite && std::isfinite (in[(size_t) i]);
                                worst = std::max (worst, std::abs (in[(size_t) i]));
                            }
                        }

                        const auto label = describe (band (shape, 0, 0, 0, slope), fs);
                        expect (finite, label);
                        expectLessThan (worst, 2000.0f, label);   // +30 dB on a 0.5 peak, with resonance at Q 40
                    }
        }
    }
};

static EqShapeTests eqShapeTests;

} // namespace encore::test
