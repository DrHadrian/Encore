// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3: the Studio EQ "Q24" (EQ-PLAN §2): its parameters
// and its bands in Zero latency.

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
    constexpr double allRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    void set (Block& b, const juce::String& id, float v) { b.findParameter (id)->setValue (v); }

    /** Puts a band in slot (0-based), in use. */
    void addBand (Eq& eq, int slot, Eq::Shape shape, float f, float gain, float q = 1.0f, int slope = 1)
    {
        set (eq, Eq::slotId (slot, "Used"), 1.0f);
        set (eq, Eq::slotId (slot, "Shape"), (float) shape);
        set (eq, Eq::slotId (slot, "Freq"), f);
        set (eq, Eq::slotId (slot, "Gain"), gain);
        set (eq, Eq::slotId (slot, "Q"), q);
        set (eq, Eq::slotId (slot, "Slope"), (float) slope);
    }

    void start (Block& b, double rate = sampleRate, int maxBlock = blockSize)
    {
        b.prepare ({ rate, maxBlock });
        b.reset();
    }

    /** Runs mono (or stereo: right = a copy) through the block in chunks;
        before each chunk, onChunk (its start sample). */
    std::vector<float> play (Block& b, std::vector<float> x, int chunk = blockSize,
                             const std::function<void (int)>& onChunk = {}, std::vector<float>* right = nullptr)
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            if (onChunk)
                onChunk (offset);

            const auto len = juce::jmin (chunk, (int) x.size() - offset);
            float* ch[2] = { x.data() + offset, right != nullptr ? right->data() + offset : nullptr };
            b.process (ch, right != nullptr ? 2 : 1, len, {});
        }

        return x;
    }

    double dftDb (const std::vector<float>& x, double f, double rate)
    {
        std::complex<double> sum, z (1.0, 0.0);
        const auto step = std::polar (1.0, -twoPi * f / rate);

        for (auto v : x)
        {
            sum += (double) v * z;
            z *= step;
        }

        return 20.0 * std::log10 (std::max (std::abs (sum), 1.0e-20));
    }

    /** The block's response at f, from an impulse. */
    double measuredDb (Eq& eq, double f, double rate, int length = 1 << 16)
    {
        std::vector<float> impulse ((size_t) length, 0.0f);
        impulse[0] = 1.0f;
        const auto out = play (eq, impulse, 512);
        return dftDb (out, f, rate);
    }

    /** Energy above a frequency (a click is broadband; the signals here are low). */
    double highBandDb (const std::vector<float>& x, double above, double rate, int from, int to)
    {
        dsp::FilterBand<1> highPass;
        highPass.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, above, 0.0, 0.7071, 96 }, rate));
        std::vector<float> y (x.begin() + from, x.begin() + to);
        float* ch[] = { y.data() };
        highPass.process (ch, 1, (int) y.size());
        return toDb (rms (y, (int) y.size() / 8, (int) y.size()));
    }

    std::vector<float> lowSine (double f, float amplitude, int n, double rate)
    {
        std::vector<float> x ((size_t) n);

        for (int i = 0; i < n; ++i)
            x[(size_t) i] = amplitude * (float) std::sin (twoPi * f * i / rate);

        return x;
    }
}

class StudioEqTests final : public juce::UnitTest
{
public:
    StudioEqTests() : juce::UnitTest ("Studio EQ", "Encore") {}

    void runTest() override
    {
        beginTest ("Parameters: 24 slots of 15 and 13 block-wide, stable ids, the plan's defaults");
        {
            Eq eq;
            expectEquals (eq.getTypeId(), juce::String ("studioEq"));
            expectEquals ((int) eq.getParameters().size(), 24 * 15 + 13);

            const char* suffixes[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place", "Mode", "Range", "Thresh",
                                       "Attack", "Release", "Side", "SideFilter" };

            for (int s = 0; s < Eq::numSlots; ++s)
                for (auto* suffix : suffixes)
                    expect (eq.findParameter (Eq::slotId (s, suffix)) != nullptr, Eq::slotId (s, suffix));

            expectEquals (Eq::slotId (0, "Freq"), juce::String ("b01Freq"));
            expectEquals (Eq::slotId (23, "Gain"), juce::String ("b24Gain"));

            for (auto* id : { "phase", "linearQuality", "character", "drive", "quality", "output", "pan", "width", "autoGain",
                              "gainScale", "invert", "cpuTight", "range" })
                expect (eq.findParameter (id) != nullptr, id);

            const auto value = [&] (const juce::String& id) { return eq.findParameter (id)->getValue(); };
            expectEquals (value ("b05Used"), 0.0f);
            expectEquals (value ("b05On"), 1.0f);
            expectEquals (value ("b05Shape"), (float) Eq::Shape::bell);
            expectEquals (value ("b05Freq"), 1000.0f);
            expectEquals (value ("b05Q"), 1.0f);
            expectEquals (value ("b05Slope"), 1.0f);   // 12 dB/oct
            expectEquals (value ("b05Thresh"), -61.0f);  // Auto
            expectEquals (value ("output"), 0.0f);
            expectEquals (value ("width"), 100.0f);
            expectEquals (value ("gainScale"), 100.0f);
            expectEquals (eq.findParameter ("autoGain")->getChoiceIndex(), (int) Eq::AutoGain::off);
            expectEquals (eq.findParameter ("autoGain")->getSpec().choices.joinIntoString (","), juce::String ("Off,Curve,Smart"));
            expectEquals (eq.findParameter ("b01Slope")->getSpec().choices[9], juce::String ("Brickwall"));
            expectEquals (eq.findParameter ("b01Shape")->getSpec().choices[7], juce::String ("Tilt shelf"));

            // The ranges.
            const auto range = [&] (const juce::String& id) { return eq.findParameter (id)->getSpec().range; };
            expectEquals (range ("b01Freq").start, 10.0f);
            expectEquals (range ("b01Freq").end, 30000.0f);
            expectEquals (range ("b01Gain").end, 30.0f);
            expectEquals (range ("b01Q").start, 0.025f);
            expectEquals (range ("b01Q").end, 40.0f);
            expectEquals (range ("output").end, 36.0f);
            expectEquals (range ("gainScale").start, -100.0f);
            expectEquals (range ("gainScale").end, 200.0f);

            // What can't go on an expression pedal (EQ-PLAN §2.9).
            for (auto* id : { "b03Used", "b03Place", "b03Mode", "b03Side", "b03SideFilter", "phase", "linearQuality", "quality",
                              "cpuTight", "range" })
                expect (! eq.findParameter (id)->getSpec().expressionAllowed, id);

            for (auto* id : { "b03Freq", "b03Gain", "b03Q", "b03On", "b03Range", "output", "gainScale", "pan", "width" })
                expect (eq.findParameter (id)->getSpec().expressionAllowed, id);
        }

        beginTest ("A new Studio EQ is bit-exact passthrough, mono and stereo; no latency, no tail");
        {
            for (auto rate : allRates)
            {
                Eq eq;
                start (eq, rate);
                const auto x = noise (0.5f, 9600, 3);
                auto right = noise (0.4f, 9600, 4);
                const auto rightIn = right;
                expect (play (eq, x) == x);
                expect (play (eq, x, blockSize, {}, &right) == x && right == rightIn);
                expectEquals (eq.getLatencySamples(), 0);
                expect (! eq.hasTail());
                expectEquals (eq.getOutputChannels (1), 1);
                expectEquals (eq.getOutputChannels (2), 2);
            }

            // A band in use at 0 dB, a band that's off, a free slot with settings: still bit-exact.
            Eq eq;
            addBand (eq, 0, Eq::Shape::bell, 500.0f, 0.0f);
            addBand (eq, 1, Eq::Shape::highShelf, 3000.0f, 9.0f);
            set (eq, "b02On", 0.0f);
            set (eq, "b03Gain", 12.0f);   // slot 3 is free
            start (eq);
            const auto x = noise (0.5f, 4800, 5);
            expect (play (eq, x) == x);
        }

        beginTest ("The bands play what responseDb says: 1, 8 and 24 bands at every rate");
        {
            for (auto rate : allRates)
            {
                for (auto count : { 1, 8, 24 })
                {
                    Eq eq;
                    const Eq::Shape shapes[] = { Eq::Shape::bell, Eq::Shape::lowShelf, Eq::Shape::highShelf, Eq::Shape::tiltShelf,
                                                 Eq::Shape::bell, Eq::Shape::notch, Eq::Shape::allPass, Eq::Shape::bandPass };

                    for (int s = 0; s < count; ++s)
                    {
                        const auto shape = count == 1 ? Eq::Shape::bell : shapes[s % 8];
                        const auto f = 40.0f * std::pow (2.0f, (float) s * 0.38f);
                        const auto gain = (s % 2 == 0 ? 1.0f : -1.0f) * (2.0f + (float) (s % 5));
                        addBand (eq, s, shape, f, gain, 0.7f + 0.1f * (float) (s % 4), shape == Eq::Shape::bandPass ? 1 : (s % 9));
                    }

                    // A band pass in series would sink everything else: keep the 8- and 24-band sets audible.
                    if (count > 1)
                        for (int s = 0; s < count; ++s)
                            if (eq.findParameter (Eq::slotId (s, "Shape"))->getChoiceIndex() == (int) Eq::Shape::bandPass)
                                set (eq, Eq::slotId (s, "Shape"), (float) Eq::Shape::bell);

                    start (eq, rate);
                    const auto designs = eq.getBandDesigns (rate);
                    expectEquals ((int) designs.size(), count);
                    auto worst = 0.0;

                    for (auto f : { 31.0, 90.0, 250.0, 700.0, 2000.0, 5500.0, 12000.0 })
                    {
                        const auto computed = dsp::responseDb (designs, f, rate);

                        if (computed > -70.0)
                            worst = std::max (worst, std::abs (measuredDb (eq, f, rate) - computed));
                    }

                    expectLessThan (worst, 0.05, juce::String (count) + " bands at " + juce::String (rate));
                }
            }
        }

        beginTest ("Gain scale: bells, shelves and the tilt scale, -100 % turns them over; cuts don't");
        {
            Eq eq;
            addBand (eq, 0, Eq::Shape::bell, 1000.0f, 6.0f);
            addBand (eq, 1, Eq::Shape::lowCut, 80.0f, 0.0f, 0.7071f, 3);
            const auto at = [&] (float scale, double f)
            {
                set (eq, "gainScale", scale);
                return dsp::responseDb (eq.getBandDesigns (48000.0), f, 48000.0);
            };

            expectWithinAbsoluteError (at (100.0f, 1000.0), 6.0, 0.01);
            expectWithinAbsoluteError (at (-100.0f, 1000.0), -6.0, 0.01);
            expectWithinAbsoluteError (at (0.0f, 1000.0), 0.0, 0.01);
            expectWithinAbsoluteError (at (200.0f, 1000.0), 12.0, 0.01);
            expectWithinAbsoluteError (at (0.0f, 80.0), -3.01, 0.05, "the cut stays");

            // It plays: measured equals the scaled response.
            set (eq, "gainScale", 150.0f);
            start (eq);
            expectWithinAbsoluteError (measuredDb (eq, 1000.0, 48000.0), 9.0 + dsp::responseDb (std::vector { eq.getBandDesigns (48000.0)[1] }, 1000.0, 48000.0), 0.05);
        }

        beginTest ("A band on, off, in and out of use glides: no click");
        {
            const auto rate = 48000.0;
            const auto n = (int) (0.6 * rate);
            const auto x = lowSine (110.0, 0.5f, n, rate);

            for (auto shape : { Eq::Shape::bell, Eq::Shape::lowCut, Eq::Shape::notch, Eq::Shape::tiltShelf })
            {
                for (auto what : { "On", "Used" })
                {
                    Eq eq;
                    addBand (eq, 4, shape, shape == Eq::Shape::lowCut ? 200.0f : 120.0f, 12.0f, 2.0f, 3);
                    start (eq);
                    const auto out = play (eq, x, blockSize, [&] (int at)
                    {
                        if (at == (int) (0.2 * rate)) set (eq, Eq::slotId (4, what), 0.0f);
                        if (at == (int) (0.4 * rate)) set (eq, Eq::slotId (4, what), 1.0f);
                    });

                    // The 110 Hz tone's own harmonics are none; a click is broadband.
                    const auto clicks = highBandDb (out, 4000.0, rate, (int) (0.15 * rate), n);
                    logMessage (juce::String ("Toggle ") + what + ", shape " + juce::String ((int) shape) + ": above 4 kHz "
                                + juce::String (clicks, 1) + " dBFS");
                    expectLessThan (clicks, -90.0, juce::String (what) + " shape " + juce::String ((int) shape));
                }
            }
        }

        beginTest ("A Shape or Slope change crossfades that band only; the others are untouched");
        {
            const auto rate = 48000.0;
            const auto x = noise (0.3f, (int) (0.3 * rate), 9);
            const auto change = (int) (0.1 * rate);
            const auto changeIt = [&] (Eq& eq, int slot)
            {
                return [&eq, slot, change] (int at)
                {
                    if (at == change)
                    {
                        set (eq, Eq::slotId (slot, "Shape"), (float) Eq::Shape::highShelf);
                        set (eq, Eq::slotId (slot, "Slope"), 5.0f);
                    }
                };
            };

            // The bands are linear and in series: both together = band 1, then band 2.
            Eq both, first, second;
            addBand (both, 0, Eq::Shape::bell, 300.0f, 6.0f);
            addBand (both, 1, Eq::Shape::lowShelf, 2000.0f, -9.0f);
            addBand (first, 0, Eq::Shape::bell, 300.0f, 6.0f);
            addBand (second, 1, Eq::Shape::lowShelf, 2000.0f, -9.0f);

            for (auto* eq : { &both, &first, &second })
                start (*eq, rate);

            const auto together = play (both, x, blockSize, changeIt (both, 1));
            const auto reference = play (second, play (first, x), blockSize, changeIt (second, 1));
            auto worst = 0.0f;

            for (size_t i = 0; i < x.size(); ++i)
                worst = std::max (worst, std::abs (together[i] - reference[i]));

            expectLessThan (worst, 1.0e-5f, "band 1 played on through band 2's crossfade");

            // And the change itself doesn't click: a low tone, a high shelf coming in.
            Eq eq;
            addBand (eq, 0, Eq::Shape::lowShelf, 3000.0f, -9.0f);
            start (eq, rate);
            const auto tone = lowSine (110.0, 0.5f, (int) (0.3 * rate), rate);
            const auto out = play (eq, tone, blockSize, changeIt (eq, 0));
            const auto clicks = highBandDb (out, 4000.0, rate, change - 2400, change + 4800);
            logMessage ("Shape and slope change on a 110 Hz tone: above 4 kHz " + juce::String (clicks, 1) + " dBFS");
            expectLessThan (clicks, -90.0);
        }

        beginTest ("Freq and Gain over their full range in one ramp: no zipper noise (against a band redesigned every sample, above 1.5 kHz)");
        {
            for (auto rate : { 48000.0, 192000.0 })
            {
                for (const juce::String sweep : { "Freq", "Gain" })
                {
                    Eq eq;
                    addBand (eq, 2, Eq::Shape::bell, 30.0f, sweep == "Gain" ? -30.0f : 12.0f, 2.0f);
                    start (eq, rate);

                    const auto n = (int) (0.1 * rate);
                    // A low tone: steps every 16 samples would land near fs/16 and up.
                    const auto x = lowSine (60.0, 0.5f, n, rate);
                    const auto at = (int) (0.02 * rate) / blockSize * blockSize;
                    const auto target = sweep == "Gain" ? 30.0f : 30000.0f;
                    const auto out = play (eq, x, blockSize, [&] (int i) { if (i == at) set (eq, Eq::slotId (2, sweep.toRawUTF8()), target); });

                    // The ideal: the same linear ramp (the parameter's 30 ms), designed every sample.
                    auto* p = eq.findParameter (Eq::slotId (2, sweep.toRawUTF8()));
                    const auto rampSamples = juce::roundToInt (p->getSpec().rampSeconds * rate);
                    dsp::FilterBand<1> ideal;
                    std::vector<float> y = x;
                    Eq::BandSettings b;
                    b.used = true;
                    b.shape = Eq::Shape::bell;
                    b.q = 2.0;
                    const auto from = sweep == "Gain" ? -30.0 : 30.0;

                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = juce::jlimit (0.0, 1.0, (i - at + 1) / (double) rampSamples);
                        // Gain ramps in dB; Freq in log steps, as a knob turns.
                        const auto v = i < at ? from : sweep == "Gain" ? from + (target - from) * t : from * std::pow (target / from, t);
                        b.frequency = sweep == "Freq" ? v : 30.0;
                        b.gainDb = sweep == "Gain" ? v : 12.0;
                        ideal.setDesign (dsp::FilterBandDesign::make (Eq::filterSettingsFor (b, 100.0), rate));
                        y[(size_t) i] = ideal.processSample (y[(size_t) i], 0);
                    }

                    std::vector<float> difference (x.size());

                    for (size_t i = 0; i < x.size(); ++i)
                        difference[i] = out[i] - y[i];

                    // The glide follows the log trajectory a little behind a
                    // per-sample redesign; that smooth lag is a slow level
                    // difference at the tone. Zipper noise would be high up.
                    const auto junk = highBandDb (difference, 1500.0, rate, at - blockSize, at + rampSamples + 4 * blockSize);
                    const auto signal = toDb (rms (y, at, at + rampSamples + 2 * blockSize));
                    logMessage (sweep + " sweep at " + juce::String (rate) + ": zipper " + juce::String (junk - signal, 1)
                                + " dB under the signal");
                    expectLessThan ((double) (junk - signal), -70.0,   // without the glide: -43 to -68 dB
                                    sweep + " at " + juce::String (rate));
                }
            }
        }

        beginTest ("24 bands at extreme settings, moving: stable, no NaN, no allocation");
        {
            Eq eq;

            for (int s = 0; s < Eq::numSlots; ++s)
                addBand (eq, s, (Eq::Shape) (s % 9), s % 2 == 0 ? 10.0f : 30000.0f, s % 3 == 0 ? 30.0f : -30.0f,
                         s % 2 == 0 ? 40.0f : 0.025f, s % 10);

            start (eq, 44100.0);
            auto x = noise (0.5f, 44100, 17);
            int flips = 0;
            std::vector<std::array<Parameter*, 4>> moved;

            for (int s = 0; s < Eq::numSlots; s += 3)
                moved.push_back ({ eq.findParameter (Eq::slotId (s, "Freq")), eq.findParameter (Eq::slotId (s, "Gain")),
                                   eq.findParameter (Eq::slotId (s, "Shape")), eq.findParameter (Eq::slotId (s, "On")) });

            const auto before = realtimeAllocationCount();
            std::vector<float> out;
            {
                const rt::ScopedRealtimeSection realtime;
                out = play (eq, std::move (x), blockSize, [&] (int at)
                {
                    if (at % 2048 == 0)
                    {
                        ++flips;

                        for (size_t k = 0; k < moved.size(); ++k)
                        {
                            moved[k][0]->setValue (flips % 2 ? 30000.0f : 10.0f);
                            moved[k][1]->setValue (flips % 2 ? -30.0f : 30.0f);
                            moved[k][2]->setValue ((float) (((int) k + flips) % 9));
                            moved[k][3]->setValue ((float) (flips % 2));
                        }
                    }
                });
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expect (std::all_of (out.begin(), out.end(), [] (float v) { return std::isfinite (v); }), "no NaN or infinity");
        }

        beginTest ("A sample-rate change re-designs everything; silence after loud input stays clean");
        {
            Eq eq;
            addBand (eq, 0, Eq::Shape::bell, 8000.0f, 9.0f, 3.0f);
            addBand (eq, 1, Eq::Shape::lowCut, 60.0f, 0.0f, 0.7071f, 7);
            start (eq, 48000.0);
            expectWithinAbsoluteError (measuredDb (eq, 8000.0, 48000.0), dsp::responseDb (eq.getBandDesigns (48000.0), 8000.0, 48000.0), 0.05);
            start (eq, 96000.0);
            expectWithinAbsoluteError (measuredDb (eq, 8000.0, 96000.0), dsp::responseDb (eq.getBandDesigns (96000.0), 8000.0, 96000.0), 0.05);

            auto loud = noise (1.0f, 9600, 21);
            loud.resize (9600 + 96000, 0.0f);
            const auto out = play (eq, loud);
            expectLessThan (std::abs (out.back()), 1.0e-9f, "rings out to silence");
        }
    }
};

static StudioEqTests studioEqTests;

} // namespace encore::test
