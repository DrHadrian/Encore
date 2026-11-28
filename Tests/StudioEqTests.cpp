// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3: the Studio EQ "Q24" (EQ-PLAN §2): its parameters
// and its bands in Zero latency.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "engine/core/FactoryPresets.h"
#include "engine/core/Realtime.h"
#include "engine/dsp/Loudness.h"

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
                             const std::function<void (int)>& onChunk = {}, std::vector<float>* right = nullptr,
                             ProcessContext context = {})
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            if (onChunk)
                onChunk (offset);

            const auto len = juce::jmin (chunk, (int) x.size() - offset);
            float* ch[2] = { x.data() + offset, right != nullptr ? right->data() + offset : nullptr };
            b.process (ch, right != nullptr ? 2 : 1, len, context);
        }

        return x;
    }

    /** Notes every second over a quiet floor: someone playing. */
    std::vector<float> playing (int seconds, double rate = sampleRate)
    {
        std::vector<float> x ((size_t) (seconds * rate), 0.0f);

        for (int k = 0; k < seconds; ++k)
        {
            const auto note = pluck ((int) rate, 0, 0.4f, 900.0, -80.0f, 82.4 * (1 + k % 3), 8, k + 1, rate);

            for (size_t i = 0; i < note.signal.size(); ++i)
                x[(size_t) k * (size_t) rate + i] += note.signal[i];
        }

        return x;
    }

    double loudnessOf (const std::vector<float>& x, double rate, int from, int to)
    {
        dsp::SlidingLoudness meter;
        meter.prepare (rate, (to - from) / rate + 1.0);
        const float* ch[] = { x.data() + from };
        meter.process (ch, 1, to - from);
        return meter.getLufs();
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
        beginTest ("Parameters: 24 slots of 19 and 14 block-wide, stable ids, the plan's defaults");
        {
            Eq eq;
            expectEquals (eq.getTypeId(), juce::String ("studioEq"));
            expectEquals ((int) eq.getParameters().size(), 24 * 19 + 14);

            // B4's 421 keep their places; B5's spectral method, then Density and Tilt per band, come after them.
            expectEquals (eq.getParameters()[420]->getSpec().id, juce::String ("b24SideQ"));
            expectEquals (eq.getParameters()[421]->getSpec().id, juce::String ("spectralMethod"));
            expectEquals (eq.getParameters()[422]->getSpec().id, juce::String ("b01Density"));
            expectEquals (eq.getParameters()[423]->getSpec().id, juce::String ("b01SpecTilt"));

            // B3's 373 keep their places; B4's sidechain frequency and Q come after them.
            expectEquals (eq.getParameters()[373]->getSpec().id, juce::String ("b01SideFreq"));
            expectEquals (eq.getParameters()[374]->getSpec().id, juce::String ("b01SideQ"));
            expectEquals (eq.getParameters()[372]->getSpec().id, juce::String ("b24SideFilter"));

            const char* suffixes[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place", "Mode", "Range", "Thresh",
                                       "Attack", "Release", "Side", "SideFilter", "SideFreq", "SideQ", "Density", "SpecTilt" };

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
            expectEquals (value ("b05SideFreq"), 1000.0f);
            expectEquals (value ("b05SideQ"), 1.0f);
            expectEquals (value ("b05Density"), 50.0f);
            expectEquals (value ("b05SpecTilt"), 1.0f);   // On
            expectEquals (value ("spectralMethod"), 0.0f); // Sub-bands
            expect (eq.findParameter ("b05Density")->getSpec().expressionAllowed);
            expect (! eq.findParameter ("b05SpecTilt")->getSpec().expressionAllowed);
            expect (! eq.findParameter ("spectralMethod")->getSpec().expressionAllowed);
            expect (eq.isPresetParameter (*eq.findParameter ("b05Density")) && eq.isPresetParameter (*eq.findParameter ("b05SpecTilt")));
            expect (! eq.isPresetParameter (*eq.findParameter ("spectralMethod")), "setup, as phase");
            expect (eq.findParameter ("b05SideFreq")->getSpec().expressionAllowed && eq.findParameter ("b05SideQ")->getSpec().expressionAllowed);
            expect (eq.isPresetParameter (*eq.findParameter ("b05SideFreq")));
            expectEquals (eq.findParameter ("b05SideFreq")->getSpec().range.start, 20.0f);
            expectEquals (eq.findParameter ("b05SideFreq")->getSpec().range.end, 20000.0f);
            expectEquals (eq.findParameter ("b05SideQ")->getSpec().range.start, 0.1f);
            expectEquals (eq.findParameter ("b05SideQ")->getSpec().range.end, 10.0f);
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
                        if (at == (int) (0.2 * rate) / blockSize * blockSize) set (eq, Eq::slotId (4, what), 0.0f);
                        if (at == (int) (0.4 * rate) / blockSize * blockSize) set (eq, Eq::slotId (4, what), 1.0f);
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
            const auto change = (int) (0.1 * rate) / blockSize * blockSize;   // a chunk's start
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

        beginTest ("K-weighting: the standard's 48 kHz coefficients, and the same curve at other rates");
        {
            const auto k48 = dsp::kWeighting (48000.0);
            expectWithinAbsoluteError (k48.sections[0].b0, 1.53512485958697, 1.0e-9);
            expectWithinAbsoluteError (k48.sections[0].b1, -2.69169618940638, 1.0e-9);
            expectWithinAbsoluteError (k48.sections[0].a1, -1.69065929318241, 1.0e-9);
            expectWithinAbsoluteError (k48.sections[1].a1, -1.99004745483398, 1.0e-9);
            expectWithinAbsoluteError (k48.sections[1].a2, 0.99007225036621, 1.0e-9);

            for (auto rate : allRates)
            {
                const auto k = dsp::kWeighting (rate);
                expectWithinAbsoluteError (k.magnitudeDb (1000.0, rate), k48.magnitudeDb (1000.0, 48000.0), 0.05);
                expectWithinAbsoluteError (k.magnitudeDb (10000.0, rate), k48.magnitudeDb (10000.0, 48000.0), 0.2);
            }
        }

        beginTest ("Output +-36 dB; on a mono signal pan and width do nothing");
        {
            Eq eq;
            set (eq, "output", 6.0f);
            set (eq, "pan", 80.0f);
            set (eq, "width", 0.0f);
            start (eq);
            const auto x = noise (0.1f, 4800, 23);
            const auto out = play (eq, x);
            expectWithinAbsoluteError (out[1000] / x[1000], juce::Decibels::decibelsToGain (6.0f), 1.0e-5f);
        }

        beginTest ("Pan (balance) and width (mid/side) on stereo; invert flips polarity with no click");
        {
            const auto l = noise (0.2f, 4800, 31), r = noise (0.2f, 4800, 32);
            const auto run = [&] (float panValue, float widthValue, bool flip)
            {
                Eq eq;
                set (eq, "pan", panValue);
                set (eq, "width", widthValue);
                set (eq, "invert", flip ? 1.0f : 0.0f);
                start (eq);
                auto right = r;
                auto left = play (eq, l, blockSize, {}, &right);
                return std::pair { left, right };
            };

            const auto [monoL, monoR] = run (0.0f, 0.0f, false);
            expectWithinAbsoluteError (monoL[100], 0.5f * (l[100] + r[100]), 1.0e-6f, "width 0: both sides are the mid");
            expectWithinAbsoluteError (monoR[100], 0.5f * (l[100] + r[100]), 1.0e-6f);

            const auto [wideL, wideR] = run (0.0f, 200.0f, false);
            expectWithinAbsoluteError (wideL[100] - wideR[100], 2.0f * (l[100] - r[100]), 1.0e-5f, "width 200: twice the side");
            expectWithinAbsoluteError (wideL[100] + wideR[100], l[100] + r[100], 1.0e-5f, "the mid stays");

            const auto [panL, panR] = run (100.0f, 100.0f, false);
            expectWithinAbsoluteError (panL[100], 0.0f, 1.0e-7f, "panned right: the left is silent");
            expectWithinAbsoluteError (panR[100], r[100], 1.0e-6f);

            const auto [flipL, flipR] = run (0.0f, 100.0f, true);
            expectWithinAbsoluteError (flipL[100], -l[100], 1.0e-6f);
            expectWithinAbsoluteError (flipR[100], -r[100], 1.0e-6f);

            // Switching it while playing crossfades through zero: no click.
            Eq eq;
            start (eq);
            const auto tone = lowSine (110.0, 0.5f, 24000, sampleRate);
            const auto out = play (eq, tone, blockSize, [&] (int at) { if (at == 75 * blockSize) set (eq, "invert", 1.0f); });
            const auto clicks = highBandDb (out, 4000.0, sampleRate, 7200, 14400);
            logMessage ("Invert while playing a 110 Hz tone: above 4 kHz " + juce::String (clicks, 1) + " dBFS");
            expectLessThan (clicks, -90.0);
            expectWithinAbsoluteError (out[20000], -tone[20000], 1.0e-6f);
        }

        beginTest ("Auto gain, Curve: a +6 dB bell over the guitar's range gives about -6 dB; a cut above 8 kHz almost nothing");
        {
            const auto curveFor = [&] (std::function<void (Eq&)> bands)
            {
                Eq eq;
                bands (eq);
                set (eq, "autoGain", (float) Eq::AutoGain::curve);
                start (eq);
                play (eq, std::vector<float> (4800, 0.0f));
                expectWithinAbsoluteError ((double) eq.getAutoGainDb(), Eq::curveGainDb (eq.getBandDesigns (sampleRate), sampleRate), 1.0e-3,
                                           "the audio's value = the response's");
                return eq.getAutoGainDb();
            };

            const auto wide = curveFor ([] (Eq& eq) { addBand (eq, 0, Eq::Shape::bell, 800.0f, 6.0f, 0.025f); });
            logMessage ("Curve: +6 dB bell, Q 0.025 at 800 Hz -> " + juce::String (wide, 2) + " dB");
            expectWithinAbsoluteError (wide, -6.0f, 0.5f);

            const auto cut = curveFor ([] (Eq& eq) { addBand (eq, 0, Eq::Shape::highCut, 10000.0f, 0.0f, 0.7071f, 3); });
            logMessage ("Curve: a high cut at 10 kHz, 24 dB/oct -> " + juce::String (cut, 2) + " dB");
            expectLessThan (std::abs (cut), 0.3f);

            // It follows a sweep, and stops computing when nothing moves.
            Eq eq;
            addBand (eq, 0, Eq::Shape::bell, 1000.0f, -12.0f, 0.7f);
            set (eq, "autoGain", (float) Eq::AutoGain::curve);
            start (eq);
            std::vector<float> seen;
            play (eq, std::vector<float> ((size_t) (0.2 * sampleRate), 0.0f), blockSize, [&] (int at)
            {
                if (at == 8 * blockSize) set (eq, Eq::slotId (0, "Gain"), 12.0f);
                seen.push_back (eq.getAutoGainDb());
            });
            logMessage ("Curve during the sweep: " + juce::String (seen.front(), 2) + " -> " + juce::String (seen.back(), 2) + " dB");
            expect (seen.front() > 1.0f && seen.back() < -1.0f, "from + to - as the bell turns over");
            expectWithinAbsoluteError ((double) seen.back(), Eq::curveGainDb (eq.getBandDesigns (sampleRate), sampleRate), 1.0e-3);
        }

        beginTest ("Auto gain, Smart: settles near equal loudness while playing; holds in silence and while tuning; at most 1 dB/s");
        {
            const auto rate = sampleRate;
            Eq eq;
            // Within Smart's reach on low notes (a 9 dB high shelf on them is past
            // its +-6 dB: the Curve value, from pink noise, is then too far off).
            addBand (eq, 0, Eq::Shape::bell, 250.0f, 6.0f);
            addBand (eq, 1, Eq::Shape::highShelf, 3000.0f, 4.0f);
            set (eq, "autoGain", (float) Eq::AutoGain::smart);
            start (eq, rate);

            const auto x = playing (20, rate);
            std::vector<float> perTenth;
            const auto out = play (eq, x, blockSize, [&] (int at)
            {
                if ((int) perTenth.size() <= at / (int) (0.1 * rate))   // the first chunk of each tenth of a second
                    perTenth.push_back (eq.getAutoGainDb());
            });

            const auto curve = Eq::curveGainDb (eq.getBandDesigns (rate), rate);
            const auto from = (int) (17.0 * rate), to = (int) (20.0 * rate);
            const auto difference = loudnessOf (out, rate, from, to) - loudnessOf (x, rate, from, to);
            logMessage ("Smart: Curve value " + juce::String (curve, 2) + " dB, now " + juce::String (eq.getAutoGainDb(), 2)
                        + " dB; loudness out - in over the last 3 s " + juce::String (difference, 2) + " dB");
            expectLessThan (std::abs (difference), 1.0, "within 1 dB of equal loudness");
            expect (std::abs (eq.getAutoGainDb() - curve) <= Eq::smartLimitDb + 1.0e-3, "within 6 dB of the Curve value");

            auto worstStep = 0.0f;

            size_t worstAt = 0;

            for (size_t i = 1; i < perTenth.size(); ++i)
                if (std::abs (perTenth[i] - perTenth[i - 1]) > worstStep)
                {
                    worstStep = std::abs (perTenth[i] - perTenth[i - 1]);
                    worstAt = i;
                }

            logMessage ("worst step at tenth " + juce::String ((int) worstAt) + ": " + juce::String (perTenth[worstAt - 1], 3) + " -> " + juce::String (perTenth[worstAt], 3));

            logMessage ("Smart: its largest move in 0.1 s " + juce::String (worstStep, 3) + " dB");
            expectLessOrEqual (worstStep, 0.1f + 1.0e-4f, "at most 1 dB per second");

            // Silence: it holds.
            const auto held = eq.getAutoGainDb();
            play (eq, std::vector<float> ((size_t) (6.0 * rate), 0.0f));
            expectWithinAbsoluteError (eq.getAutoGainDb(), held, 1.0e-6f, "no climb in silence");

            // The tuner open, someone tuning loudly: it holds.
            ProcessContext tuning;
            tuning.tunerOpen = true;
            auto loud = playing (6, rate);

            for (auto& v : loud)
                v *= 3.0f;

            play (eq, loud, blockSize, {}, nullptr, tuning);
            expectWithinAbsoluteError (eq.getAutoGainDb(), held, 1.0e-6f, "frozen while the tuner is open");
        }

        beginTest ("Factory presets: each loads its bands into slots 01, 02 ...; Flat is passthrough; the setup isn't part of one");
        {
            struct Expected { const char* id; std::vector<Eq::BandSettings> bands; };
            const auto band = [] (Eq::Shape shape, double f, double gain, double q, int slope)
            {
                Eq::BandSettings b;
                b.used = true;
                b.shape = shape;
                b.frequency = f;
                b.gainDb = gain;
                b.q = q;
                b.slope = slope;
                return b;
            };

            const Expected expected[] {
                { "flat", {} },
                { "tightenLows", { band (Eq::Shape::lowCut, 90.0, 0.0, 1.0, 3), band (Eq::Shape::lowShelf, 150.0, -2.0, 0.7071, 1) } },
                { "cutMud", { band (Eq::Shape::bell, 300.0, -3.0, 1.2, 1) } },
                { "presence", { band (Eq::Shape::bell, 3000.0, 2.5, 0.9, 1) } },
                { "tameFizz", { band (Eq::Shape::bell, 5500.0, 0.0, 2.0, 1), band (Eq::Shape::highCut, 8000.0, 0.0, 1.0, 1) } },
                { "pickAttack", { band (Eq::Shape::bell, 2500.0, 0.0, 1.0, 1) } },
                { "sitInMix", { band (Eq::Shape::lowCut, 100.0, 0.0, 1.0, 2), band (Eq::Shape::bell, 250.0, -2.0, 1.0, 1),
                                band (Eq::Shape::bell, 2500.0, 2.0, 1.0, 1), band (Eq::Shape::highCut, 9000.0, 0.0, 1.0, 1) } },
            };

            Eq ids;
            expectEquals (presets::listIds (ids), juce::String ("flat, tightenLows, cutMud, presence, tameFizz, pickAttack, sitInMix"));

            for (const auto& e : expected)
            {
                Eq eq;

                // Something to clear first: a stray band, a setting, the setup.
                addBand (eq, 9, Eq::Shape::notch, 777.0f, 0.0f);
                set (eq, "gainScale", 50.0f);
                set (eq, "width", 150.0f);
                set (eq, "pan", -30.0f);

                presets::apply (eq, *presets::find (eq, e.id));
                expectEquals (eq.getNumBandsInUse(), (int) e.bands.size(), e.id);
                expectEquals (eq.findParameter ("gainScale")->getValue(), 100.0f, "a preset parameter: to its default");
                expectEquals (eq.findParameter ("width")->getValue(), 150.0f, "the setup stays");
                expectEquals (eq.findParameter ("pan")->getValue(), -30.0f);

                std::vector<dsp::FilterBandDesign> listed;

                for (size_t i = 0; i < e.bands.size(); ++i)
                {
                    const auto got = eq.getBand ((int) i);
                    expect (got.used && got.shape == e.bands[i].shape, e.id);
                    listed.push_back (dsp::FilterBandDesign::make (Eq::filterSettingsFor (e.bands[i], 100.0), sampleRate));
                }

                const auto designs = eq.getBandDesigns (sampleRate);

                for (auto f : { 40.0, 120.0, 300.0, 1000.0, 3000.0, 9000.0, 15000.0 })
                    expectWithinAbsoluteError (dsp::responseDb (designs, f, sampleRate), dsp::responseDb (listed, f, sampleRate), 1.0e-4, e.id);

                if (juce::String (e.id) == "flat")
                {
                    // With the setup neutral again, Flat is passthrough.
                    set (eq, "width", 100.0f);
                    set (eq, "pan", 0.0f);
                    start (eq);
                    const auto x = noise (0.4f, 4800, 41);
                    auto right = noise (0.4f, 4800, 42);
                    const auto rightIn = right;
                    expect (play (eq, x, blockSize, {}, &right) == x && right == rightIn, "Flat is bit-exact");
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

            // Everything set up before the realtime section (a std::function
            // or a moved vector may allocate, in a Debug build especially).
            auto out = x;
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int at = 0; at < (int) out.size(); at += blockSize)
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

                    float* ch[] = { out.data() + at };
                    eq.process (ch, 1, juce::jmin (blockSize, (int) out.size() - at), {});
                }
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
