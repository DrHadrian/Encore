// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B4: the Studio EQ's character (EQ-PLAN §2.4):
// Clean, Soft and Warm with Drive, oversampled by Quality.

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

    std::vector<float> play (Block& b, std::vector<float> x, int chunk = blockSize)
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            float* ch[] = { x.data() + offset };
            b.process (ch, 1, juce::jmin (chunk, (int) x.size() - offset), {});
        }

        return x;
    }

    /** An EQ with no bands, this character, Drive and Quality, prepared. */
    void setUp (Eq& eq, Eq::Character character, float driveDb, Eq::Quality q = Eq::Quality::live)
    {
        set (eq, "character", (float) character);
        set (eq, "drive", driveDb);
        set (eq, "quality", (float) q);
        eq.prepare (spec);
        eq.reset();
    }

    /** The amplitude of harmonic h of a 1 kHz tone (48 samples a period) over
        a whole number of periods, relative to the fundamental's (dB). */
    struct Harmonics
    {
        double fundamentalDb = 0.0;
        std::array<double, 8> relativeDb {};   // [h] for h = 2..7
        double thdDb = -300.0;                 // harmonics 2..7 together
    };

    Harmonics harmonicsOf (const std::vector<float>& y, int from, int periods)
    {
        const auto amplitude = [&] (int h)
        {
            std::complex<double> sum;

            for (int i = 0; i < periods * 48; ++i)
                sum += (double) y[(size_t) (from + i)] * std::polar (1.0, -twoPi * h * i / 48.0);

            return 2.0 * std::abs (sum) / (periods * 48);
        };

        Harmonics result;
        const auto fundamental = amplitude (1);
        result.fundamentalDb = 20.0 * std::log10 (fundamental);
        auto power = 0.0;

        for (int h = 2; h <= 7; ++h)
        {
            const auto a = amplitude (h);
            result.relativeDb[(size_t) h] = 20.0 * std::log10 (std::max (a, 1.0e-15) / fundamental);
            power += a * a;
        }

        result.thdDb = 10.0 * std::log10 (std::max (power, 1.0e-30) / (fundamental * fundamental));
        return result;
    }

    Harmonics measure (Eq::Character character, float driveDb, float levelDbfs, Eq::Quality q = Eq::Quality::high)
    {
        Eq eq;
        setUp (eq, character, driveDb, q);
        const auto y = play (eq, sine (1000.0, juce::Decibels::decibelsToGain (levelDbfs), 24000));
        return harmonicsOf (y, 9600, 200);
    }

    /** Aliasing: a sine exactly on FFT bin `bin` (of 32768 at 48 kHz); the
        power in every bin that isn't a harmonic (or DC), relative to the total. */
    double aliasingDb (Block& block, int bin, float amplitude)
    {
        constexpr int order = 15, size = 1 << order;
        const auto frequency = bin * sampleRate / size;
        std::vector<float> x ((size_t) size * 2);

        for (size_t i = 0; i < x.size(); ++i)
            x[i] = amplitude * (float) std::sin (twoPi * frequency * (double) i / sampleRate);

        // Three windows of settling first: an asymmetric curve driven hard
        // makes DC that the 1 Hz blocker takes a while to remove.
        std::vector<float> x4 ((size_t) size * 4);

        for (size_t i = 0; i < x4.size(); ++i)
            x4[i] = amplitude * (float) std::sin (twoPi * frequency * (double) i / sampleRate);

        const auto out = play (block, x4);
        juce::ignoreUnused (x);
        std::vector<float> fft ((size_t) size * 2, 0.0f);
        std::copy (out.begin() + size * 3, out.end(), fft.begin());
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (fft.data());

        double total = 0.0, harmonics = 0.0;

        // Aliasing below 16 kHz: above it, near Nyquist, what's left is the
        // anti-alias filter's transition band (the same at every factor).
        const auto top = (int) (16000.0 * size / sampleRate);

        for (int b = 1; b < size / 2; ++b)
        {
            const auto p = (double) fft[(size_t) b] * fft[(size_t) b];
            total += p;

            if (b % bin == 0 || b <= 2 || b >= top)
                harmonics += p;
        }

        return 10.0 * std::log10 (std::max (total - harmonics, 1.0e-30) / total);
    }

    double highBandDb (const std::vector<float>& x, double above, int from, int to)
    {
        dsp::FilterBand<1> highPass;
        highPass.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, above, 0.0, 0.7071, 96 }, sampleRate));
        std::vector<float> y (x.begin(), x.begin() + to);
        float* ch[] = { y.data() };
        highPass.process (ch, 1, (int) y.size());
        return juce::Decibels::gainToDecibels (rms (y, from, to), -300.0f)
               - juce::Decibels::gainToDecibels (rms (x, from, to), -300.0f);
    }
}

class StudioEqCharacterTests final : public juce::UnitTest
{
public:
    StudioEqCharacterTests() : juce::UnitTest ("Studio EQ character", "Encore") {}

    void runTest() override
    {
        beginTest ("Clean is not in the path: bit-exact, no latency, at any Drive and Quality");
        {
            Eq eq;
            setUp (eq, Eq::Character::clean, 24.0f, Eq::Quality::max);
            const auto x = noise (0.9f, 9600, 2);
            expect (play (eq, x) == x);
            expectEquals (eq.getLatencySamples(), 0);
            expect (! eq.isCharacterInPath());
        }

        beginTest ("Soft: odd-order only (even harmonics below -90 dB); Warm: the second harmonic is its largest");
        {
            for (auto driveDb : { 0.0f, 12.0f, 24.0f })
            {
                const auto h = measure (Eq::Character::soft, driveDb, -6.0f);
                expectLessThan (h.relativeDb[2], -90.0, "Soft H2 at Drive " + juce::String (driveDb));
                expectLessThan (h.relativeDb[4], -90.0, "Soft H4 at Drive " + juce::String (driveDb));
                expectLessThan (h.relativeDb[6], -90.0, "Soft H6 at Drive " + juce::String (driveDb));
            }

            const auto soft = measure (Eq::Character::soft, 12.0f, -6.0f);
            logMessage ("Soft, Drive 12, -6 dBFS: H2 " + juce::String (soft.relativeDb[2], 1) + " dB, H3 "
                        + juce::String (soft.relativeDb[3], 1) + " dB");

            for (auto driveDb : { 0.0f, 6.0f, 12.0f, 18.0f })
            {
                const auto h = measure (Eq::Character::warm, driveDb, -18.0f);
                logMessage ("Warm, Drive " + juce::String (driveDb) + ", -18 dBFS: H2 " + juce::String (h.relativeDb[2], 1) + " H3 "
                            + juce::String (h.relativeDb[3], 1) + " H4 " + juce::String (h.relativeDb[4], 1) + " dB");

                for (int k = 3; k <= 7; ++k)
                    expectGreaterThan (h.relativeDb[2], h.relativeDb[(size_t) k],
                                       "Warm H2 over H" + juce::String (k) + " at Drive " + juce::String (driveDb));
            }
        }

        beginTest ("THD rises with Drive; a -30 dBFS sine keeps its level within 0.5 dB at every Drive and Quality");
        {
            for (auto character : { Eq::Character::soft, Eq::Character::warm })
            {
                auto previous = -300.0;
                juce::String line;

                for (auto driveDb : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
                {
                    const auto thd = measure (character, driveDb, -10.0f).thdDb;
                    line << juce::String (thd, 1) << " ";
                    expectGreaterThan (thd, previous + 1.0, "rises at Drive " + juce::String (driveDb));
                    previous = thd;

                    for (auto q : { Eq::Quality::live, Eq::Quality::high, Eq::Quality::max })
                    {
                        const auto level = measure (character, driveDb, -30.0f, q).fundamentalDb;
                        expectWithinAbsoluteError (level, -30.0, 0.5, "level at Drive " + juce::String (driveDb));
                    }
                }

                logMessage (juce::String (character == Eq::Character::soft ? "Soft" : "Warm")
                            + " THD at -10 dBFS, Drive 0/6/12/18/24: " + line + "dB");
            }
        }

        beginTest ("Aliasing falls with Quality");
        {
            const auto aliasing = [] (Eq::Character character, Eq::Quality q)
            {
                Eq eq;
                setUp (eq, character, 24.0f, q);
                return aliasingDb (eq, 1707, 1.0f);   // 2.5 kHz at 0 dBFS, Drive 24: worked hard
            };

            for (auto character : { Eq::Character::soft, Eq::Character::warm })
            {
                const auto live = aliasing (character, Eq::Quality::live), high = aliasing (character, Eq::Quality::high),
                           max = aliasing (character, Eq::Quality::max);
                logMessage (juce::String (character == Eq::Character::soft ? "Soft" : "Warm") + " aliasing (dB below the signal): Live "
                            + juce::String (live, 1) + ", High " + juce::String (high, 1) + ", Max " + juce::String (max, 1));
                expectLessThan (high, live - 3.0);
                expectLessThan (max, high - 3.0);
            }
        }

        beginTest ("Latency: reported per Quality while in the path; in and out crossfade with no click");
        {
            std::array<int, 3> latencies {};

            for (int q = 0; q < 3; ++q)
            {
                dsp::Oversampler reference;
                reference.prepare (2, blockSize, sampleRate);
                reference.resetTo ((dsp::Quality) q);

                Eq eq;
                setUp (eq, Eq::Character::soft, 0.0f, (Eq::Quality) q);
                expectEquals (eq.getLatencySamples(), reference.getLatencySamples(), "from the start");
                std::vector<float> x (blockSize, 0.0f);
                play (eq, x);
                latencies[(size_t) q] = eq.getLatencySamples();
                expectEquals (latencies[(size_t) q], reference.getLatencySamples(), "Quality " + juce::String (q));
                expect (eq.isCharacterInPath());
            }

            logMessage ("Character latency, Live / High / Max: " + juce::String (latencies[0]) + " / " + juce::String (latencies[1])
                        + " / " + juce::String (latencies[2]) + " samples at 48 kHz");
            expect (latencies[0] > 0 && latencies[1] >= latencies[0] && latencies[2] >= latencies[1]);

            // Clean -> Warm -> Soft -> Clean, and High -> Max, while a low tone plays.
            Eq eq;
            setUp (eq, Eq::Character::clean, 6.0f);
            auto x = sine (200.0, 0.1f, 48000);
            std::vector<int> latency;
            latency.reserve (48000 / blockSize + 1);
            auto* characterParameter = eq.findParameter ("character");
            auto* qualityParameter = eq.findParameter ("quality");
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int at = 0; at < 48000; at += blockSize)
                {
                    if (at == 9600)  characterParameter->setValue ((float) Eq::Character::warm);
                    if (at == 19200) characterParameter->setValue ((float) Eq::Character::soft);
                    if (at == 28800) qualityParameter->setValue ((float) Eq::Quality::max);
                    if (at == 38400) characterParameter->setValue ((float) Eq::Character::clean);

                    float* ch[] = { x.data() + at };
                    eq.process (ch, 1, juce::jmin (blockSize, 48000 - at), {});
                    latency.push_back (eq.getLatencySamples());
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expectEquals (latency[9600 / blockSize - 1], 0);
            expectEquals (latency[9600 / blockSize], latencies[0], "in: Live's latency");
            expectEquals (latency[28800 / blockSize + 1], latencies[2], "Max's latency");
            expectEquals (latency.back(), 0, "out: none");

            const auto click = highBandDb (x, 4000.0, 2400, 48000);
            logMessage ("Character in, across, Quality and out on a 200 Hz tone: energy above 4 kHz " + juce::String (click, 1) + " dB");
            expectLessThan (click, -60.0, "no click");
        }
    }
};

static StudioEqCharacterTests studioEqCharacterTests;

} // namespace encore::test
