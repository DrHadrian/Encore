// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B5: the Studio EQ's spectral bands (EQ-PLAN §2.2).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "engine/core/Realtime.h"

#include <complex>

namespace encore::test
{

namespace
{
    using Eq = StudioEqBlock;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    void set (Block& b, const juce::String& id, float v) { b.findParameter (id)->setValue (v); }
    void setSlot (Block& b, int slot, const char* suffix, float v) { set (b, Eq::slotId (slot, suffix), v); }

    void spectralBell (Eq& eq, float f, float q, float range, float threshold, Eq::SpectralMethod method,
                       float density = 50.0f, bool tilt = false, float attack = 2.0f, float release = 50.0f)
    {
        setSlot (eq, 0, "Used", 1.0f);
        setSlot (eq, 0, "Freq", f);
        setSlot (eq, 0, "Q", q);
        setSlot (eq, 0, "Mode", (float) Eq::Mode::spectral);
        setSlot (eq, 0, "Range", range);
        setSlot (eq, 0, "Thresh", threshold);
        setSlot (eq, 0, "Attack", attack);
        setSlot (eq, 0, "Release", release);
        setSlot (eq, 0, "Density", density);
        setSlot (eq, 0, "SpecTilt", tilt ? 1.0f : 0.0f);
        set (eq, "spectralMethod", (float) method);
    }

    void start (Block& b, int maxBlock = 64)
    {
        b.prepare ({ sampleRate, maxBlock });
        b.reset();
    }

    std::vector<float> play (Block& b, std::vector<float> x, int chunk = 64)
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            float* ch[] = { x.data() + offset };
            b.process (ch, 1, juce::jmin (chunk, (int) x.size() - offset), {});
        }

        return x;
    }

    std::vector<float> tones (std::initializer_list<std::pair<double, double>> list, int n)
    {
        std::vector<float> x ((size_t) n, 0.0f);

        for (const auto& [f, rmsDb] : list)
        {
            const auto a = std::sqrt (2.0) * juce::Decibels::decibelsToGain (rmsDb);

            for (int i = 0; i < n; ++i)
                x[(size_t) i] += (float) (a * std::sin (twoPi * f * i / sampleRate));
        }

        return x;
    }

    /** A tone's level in y relative to x (dB), over [from, to), y shifted back by delay. */
    double gainAt (const std::vector<float>& x, const std::vector<float>& y, double f, int from, int to, int delay = 0)
    {
        std::complex<double> a, b;

        for (int i = from; i < to; ++i)
        {
            const auto w = std::polar (1.0, -twoPi * f * i / sampleRate);
            a += (double) x[(size_t) i] * w;
            b += (double) y[(size_t) (i + delay)] * w;
        }

        return 20.0 * std::log10 (std::abs (b) / std::abs (a));
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
}

class StudioEqSpectralTests final : public juce::UnitTest
{
public:
    StudioEqSpectralTests() : juce::UnitTest ("Studio EQ spectral", "Encore") {}

    void runTest() override
    {
        beginTest ("Sub-bands: only the loud tone's region dips; the rest of the band stays within 0.5 dB; no latency");
        {
            Eq eq;
            spectralBell (eq, 1000.0f, 0.7f, -9.0f, -30.0f, Eq::SpectralMethod::subBands);
            start (eq);
            const auto x = tones ({ { 1000.0, -12.0 }, { 600.0, -45.0 }, { 1600.0, -45.0 } }, 48000);
            const auto y = play (eq, x);
            expectEquals (eq.getLatencySamples(), 0);
            const auto loud = gainAt (x, y, 1000.0, 24000, 48000), low = gainAt (x, y, 600.0, 24000, 48000),
                       high = gainAt (x, y, 1600.0, 24000, 48000);
            logMessage ("Sub-bands, a loud 1 kHz tone in a 1 kHz spectral bell (Range -9): 1 kHz " + juce::String (loud, 2)
                        + " dB, 600 Hz " + juce::String (low, 2) + " dB, 1.6 kHz " + juce::String (high, 2) + " dB; "
                        + juce::String (eq.getNumSubBands (0)) + " sub-bands");
            expectLessThan (loud, -6.0, "the loud tone dips");
            expectWithinAbsoluteError (low, 0.0, 0.5);
            expectWithinAbsoluteError (high, 0.0, 0.5);
        }

        beginTest ("Density narrows the dip");
        {
            const auto dipBeside = [] (float density)
            {
                Eq eq;
                spectralBell (eq, 1000.0f, 1.5f, -9.0f, -30.0f, Eq::SpectralMethod::subBands, density);
                start (eq);
                const auto x = tones ({ { 1000.0, -12.0 }, { 1189.0, -50.0 } }, 48000);   // a probe a quarter octave up
                const auto y = play (eq, x);
                return gainAt (x, y, 1189.0, 24000, 48000);
            };

            const auto wide = dipBeside (0.0f), mid = dipBeside (50.0f), narrow = dipBeside (100.0f);
            logMessage ("A quarter octave beside the loud tone: Density 0 " + juce::String (wide, 2) + " dB, 50 " + juce::String (mid, 2)
                        + " dB, 100 " + juce::String (narrow, 2) + " dB");
            expectLessThan (wide, narrow - 1.0);
            expectGreaterThan (narrow, -1.0);
        }

        beginTest ("Tilt: highs trigger a little more readily");
        {
            const auto deepest = [] (bool tilt)
            {
                Eq eq;
                spectralBell (eq, 2000.0f, 0.5f, -9.0f, -30.0f, Eq::SpectralMethod::subBands, 50.0f, tilt);
                start (eq);
                play (eq, tones ({ { 3500.0, -31.5 } }, 24000));   // 1.5 dB under the threshold
                auto lowest = 0.0f;

                for (int k = 0; k < eq.getNumSubBands (0); ++k)
                    lowest = std::min (lowest, eq.getSubBandGainDb (0, k));

                return lowest;
            };

            const auto off = deepest (false), on = deepest (true);
            logMessage ("A 3.5 kHz tone 1.5 dB under the threshold in a 2 kHz band: Tilt off " + juce::String (off, 2) + " dB, on "
                        + juce::String (on, 2) + " dB");
            expectLessThan ((double) on, (double) off - 0.5);
        }

        beginTest ("Sub-bands at rest cost nothing: Range 0 is bit-exact static; under the threshold no sub-band plays");
        {
            const auto x = noise (0.3f, 24000, 9);

            const auto render = [&] (Eq::Mode mode, float range)
            {
                Eq eq;
                spectralBell (eq, 1000.0f, 0.7f, range, -10.0f, Eq::SpectralMethod::subBands);
                setSlot (eq, 0, "Gain", 4.0f);
                setSlot (eq, 0, "Mode", (float) mode);
                start (eq);
                return play (eq, x);
            };

            expect (render (Eq::Mode::spectral, 0.0f) == render (Eq::Mode::staticBand, -9.0f), "Range 0 = static");

            Eq quiet;
            spectralBell (quiet, 1000.0f, 0.7f, -9.0f, -10.0f, Eq::SpectralMethod::subBands);
            start (quiet);
            play (quiet, tones ({ { 1000.0, -40.0 } }, 24000));
            auto moving = 0;

            for (int k = 0; k < quiet.getNumSubBands (0); ++k)
                if (quiet.getSubBandGainDb (0, k) != 0.0f)
                    ++moving;

            expectEquals (moving, 0, "30 dB under the threshold: no sub-band moves");
        }

        beginTest ("Sub-bands moving: no zipper noise; no allocation; in Linear phase their movement is in the kernel");
        {
            Eq eq;
            spectralBell (eq, 300.0f, 0.7f, -12.0f, -30.0f, Eq::SpectralMethod::subBands, 50.0f, false, 10.0f, 30.0f);
            setSlot (eq, 0, "Side", (float) Eq::Side::dryGuitar);   // keyed by bursts; what plays is a steady tone
            start (eq);
            const auto x = sine (300.0, 0.25f, 96000);
            std::vector<float> dry (96000, 0.0f);

            for (int i = 0; i < 96000; ++i)
                dry[(size_t) i] = (i / 4800) % 2 == 0 ? 0.5f * (float) std::sin (twoPi * 300.0 * i / sampleRate) : 0.0f;

            auto y = x;
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int at = 0; at < 96000; at += 64)
                {
                    float* ch[] = { y.data() + at };
                    ProcessContext context;
                    context.chainInput = dry.data() + at;
                    eq.process (ch, 1, 64, context);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            const auto high = highBandDb (y, 6000.0, 4800, 96000);
            logMessage ("Sub-bands dipping 12 dB every 100 ms on 300 Hz: energy above 6 kHz " + juce::String (high, 1) + " dB");
            auto lowest = 0.0f;

            for (int at = 0; at < 96000; at += 4800)
                lowest = std::min (lowest, (float) juce::Decibels::gainToDecibels (rms (y, at + 2400, at + 4800) / 0.1768f));

            expectLessThan (lowest, -6.0f, "it moved");
            expectLessThan (high, -85.0);

            Eq linear;
            spectralBell (linear, 1000.0f, 0.7f, -9.0f, -30.0f, Eq::SpectralMethod::subBands);
            set (linear, "phase", (float) Eq::Phase::linear);
            start (linear);
            const auto latency = linear.getLatencySamples();
            const auto t = tones ({ { 1000.0, -12.0 }, { 600.0, -45.0 } }, 48000);
            const auto z = play (linear, t);
            const auto loud = gainAt (t, z, 1000.0, 24000, 46000, latency), low = gainAt (t, z, 600.0, 24000, 46000, latency);
            logMessage ("Linear phase: 1 kHz " + juce::String (loud, 2) + " dB, 600 Hz " + juce::String (low, 2) + " dB");
            expectLessThan (loud, -6.0);
            expectWithinAbsoluteError (low, 0.0, 0.5);
        }

        beginTest ("FFT: a single loud bin dips, a quiet one beside it doesn't; the latency is the stage's in Zero latency");
        {
            Eq eq;
            spectralBell (eq, 1500.0f, 0.7f, -9.0f, -30.0f, Eq::SpectralMethod::fft, 100.0f);
            start (eq);
            // A Medium bin is 46.875 Hz: tones on bins 32 (1500 Hz) and 36 (1687.5 Hz).
            const auto x = tones ({ { 1500.0, -12.0 }, { 1687.5, -50.0 }, { 1218.75, -50.0 } }, 96000);
            const auto y = play (eq, x);
            const auto latency = eq.getLatencySamples();
            expectEquals (latency, 576, "the stage's latency, in Zero latency mode");
            const auto loud = gainAt (x, y, 1500.0, 48000, 90000, latency), above = gainAt (x, y, 1687.5, 48000, 90000, latency),
                       below = gainAt (x, y, 1218.75, 48000, 90000, latency);
            logMessage ("FFT, Density 100, a loud 1.5 kHz bin: " + juce::String (loud, 2) + " dB; 4 bins up " + juce::String (above, 2)
                        + " dB, 6 bins down " + juce::String (below, 2) + " dB; the bell's movement at 1.5 kHz "
                        + juce::String (eq.getSpectralGainDb (0, 1500.0), 2) + " dB");
            expectLessThan (loud, -6.0);
            expectWithinAbsoluteError (above, 0.0, 1.0);
            expectWithinAbsoluteError (below, 0.0, 1.0);

            Eq linear;
            spectralBell (linear, 1500.0f, 0.7f, -9.0f, -30.0f, Eq::SpectralMethod::fft, 100.0f);
            set (linear, "phase", (float) Eq::Phase::linear);
            start (linear);
            const auto z = play (linear, x);
            expectEquals (linear.getLatencySamples(), 576, "in Linear phase it joins the kernel: once");
            expectLessThan (gainAt (x, z, 1500.0, 48000, 90000, 576), -6.0);
        }

        beginTest ("FFT and Sub-bands agree in broad effect; FFT allocates nothing");
        {
            const auto measure = [] (Eq::SpectralMethod method)
            {
                Eq eq;
                spectralBell (eq, 1000.0f, 0.7f, -9.0f, -30.0f, method);
                start (eq);
                auto x = tones ({ { 1000.0, -12.0 }, { 600.0, -45.0 }, { 1600.0, -45.0 } }, 96000);
                auto y = x;
                const auto before = realtimeAllocationCount();
                {
                    const rt::ScopedRealtimeSection realtime;

                    for (int at = 0; at < 96000; at += 64)
                    {
                        float* ch[] = { y.data() + at };
                        eq.process (ch, 1, 64, {});
                    }
                }
                const auto allocations = realtimeAllocationCount() - before;
                const auto latency = eq.getLatencySamples();
                return std::array<double, 4> { gainAt (x, y, 1000.0, 48000, 90000, latency), gainAt (x, y, 600.0, 48000, 90000, latency),
                                               gainAt (x, y, 1600.0, 48000, 90000, latency), (double) allocations };
            };

            const auto sub = measure (Eq::SpectralMethod::subBands), fft = measure (Eq::SpectralMethod::fft);
            logMessage ("The same loud 1 kHz tone: Sub-bands " + juce::String (sub[0], 2) + " / " + juce::String (sub[1], 2) + " / "
                        + juce::String (sub[2], 2) + " dB, FFT " + juce::String (fft[0], 2) + " / " + juce::String (fft[1], 2) + " / "
                        + juce::String (fft[2], 2) + " dB (1 kHz / 600 Hz / 1.6 kHz)");
            expectWithinAbsoluteError (fft[0], sub[0], 3.0, "the loud tone: within 3 dB");
            expectWithinAbsoluteError (fft[1], sub[1], 0.5);
            expectWithinAbsoluteError (fft[2], sub[2], 0.5);
            expectEquals ((int) fft[3], 0);
        }
    }
};

static StudioEqSpectralTests studioEqSpectralTests;

} // namespace encore::test
