// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B4: the Studio EQ's dynamic bands and their sidechain
// (EQ-PLAN §2.2).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    using Eq = StudioEqBlock;

    void set (Block& b, const juce::String& id, float v) { b.findParameter (id)->setValue (v); }
    void setSlot (Block& b, int slot, const char* suffix, float v) { set (b, Eq::slotId (slot, suffix), v); }

    /** A dynamic band in slot (0-based). */
    void addDynamic (Eq& eq, int slot, Eq::Shape shape, float f, float range, float threshold, float attackMs = 1.0f,
                     float releaseMs = 20.0f, float q = 1.0f, float gain = 0.0f)
    {
        setSlot (eq, slot, "Used", 1.0f);
        setSlot (eq, slot, "Shape", (float) shape);
        setSlot (eq, slot, "Freq", f);
        setSlot (eq, slot, "Gain", gain);
        setSlot (eq, slot, "Q", q);
        setSlot (eq, slot, "Mode", (float) Eq::Mode::dynamic);
        setSlot (eq, slot, "Range", range);
        setSlot (eq, slot, "Thresh", threshold);
        setSlot (eq, slot, "Attack", attackMs);
        setSlot (eq, slot, "Release", releaseMs);
    }

    void start (Block& b, double rate = sampleRate, int maxBlock = blockSize)
    {
        b.prepare ({ rate, maxBlock });
        b.reset();
    }

    /** Mono through the block in chunks; after each, onChunk (its end sample). */
    std::vector<float> play (Block& b, std::vector<float> x, int chunk = blockSize, const float* dry = nullptr,
                             const std::function<void (int)>& onChunk = {}, bool tunerOpen = false)
    {
        for (int offset = 0; offset < (int) x.size(); offset += chunk)
        {
            const auto len = juce::jmin (chunk, (int) x.size() - offset);
            float* ch[] = { x.data() + offset };
            ProcessContext context;
            context.chainInput = dry != nullptr ? dry + offset : nullptr;
            context.tunerOpen = tunerOpen;
            b.process (ch, 1, len, context);

            if (onChunk)
                onChunk (offset + len);
        }

        return x;
    }

    /** A sine of RMS `rmsDb` dBFS. */
    std::vector<float> sineRms (double f, double rmsDb, int n)
    {
        return sine (f, (float) (std::sqrt (2.0) * juce::Decibels::decibelsToGain (rmsDb)), n);
    }

    /** Energy above a frequency, relative to the whole signal (dB). */
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

class StudioEqDynamicsTests final : public juce::UnitTest
{
public:
    StudioEqDynamicsTests() : juce::UnitTest ("Studio EQ dynamics", "Encore") {}

    void runTest() override
    {
        beginTest ("The law: 1 dB per dB over the threshold, a 6 dB soft knee, capped at |Range|, in Range's direction");
        {
            expectEquals (Eq::movementDb (-10.0, -6.0), 0.0);
            expectEquals (Eq::movementDb (-3.0, -6.0), 0.0);
            expectWithinAbsoluteError (Eq::movementDb (0.0, -6.0), -0.75, 1.0e-12);   // the knee's middle: (3)^2 / 12
            expectWithinAbsoluteError (Eq::movementDb (3.0, -6.0), -3.0, 1.0e-12);
            expectWithinAbsoluteError (Eq::movementDb (4.0, -6.0), -4.0, 1.0e-12);
            expectWithinAbsoluteError (Eq::movementDb (20.0, -6.0), -6.0, 1.0e-12);
            expectWithinAbsoluteError (Eq::movementDb (20.0, 3.0), 3.0, 1.0e-12);
            expectEquals (Eq::movementDb (20.0, 0.0), 0.0);

            expect (Eq::canBeDynamic (Eq::Shape::bell) && Eq::canBeDynamic (Eq::Shape::lowShelf)
                    && Eq::canBeDynamic (Eq::Shape::highShelf) && Eq::canBeDynamic (Eq::Shape::tiltShelf));
            expect (! Eq::canBeDynamic (Eq::Shape::lowCut) && ! Eq::canBeDynamic (Eq::Shape::highCut)
                    && ! Eq::canBeDynamic (Eq::Shape::notch) && ! Eq::canBeDynamic (Eq::Shape::bandPass)
                    && ! Eq::canBeDynamic (Eq::Shape::allPass));
        }

        beginTest ("On a steady sine the band settles where the law says, and plays that gain");
        {
            for (auto rmsDb : { -45.0, -33.0, -30.0, -27.0, -20.0, -6.0 })
            {
                Eq eq;
                addDynamic (eq, 0, Eq::Shape::bell, 1000.0f, -12.0f, -30.0f);
                start (eq);
                const auto x = sineRms (1000.0, rmsDb, 24000);
                const auto y = play (eq, x);
                const auto expected = Eq::movementDb (rmsDb + 30.0, -12.0);
                expectWithinAbsoluteError ((double) eq.getDynamicGainDb (0), expected, 0.15, "movement at " + juce::String (rmsDb));
                expectWithinAbsoluteError ((double) eq.getLiveGainDb (0), expected, 0.15);

                // The band plays it: a bell at 1 kHz moved by `expected` dB.
                const auto gain = juce::Decibels::gainToDecibels (rms (y, 19200, 24000) / rms (x, 19200, 24000));
                expectWithinAbsoluteError ((double) gain, expected, 0.2, "heard at " + juce::String (rmsDb));
            }

            // Upwards: Range +6, loud: +6.
            Eq up;
            addDynamic (up, 0, Eq::Shape::bell, 1000.0f, 6.0f, -40.0f);
            start (up);
            play (up, sineRms (1000.0, -10.0, 24000));
            expectWithinAbsoluteError ((double) up.getDynamicGainDb (0), 6.0, 0.05);

            // The movement starts from the band's own Gain: Gain +4, Range -6 -> -2 while loud.
            Eq from;
            addDynamic (from, 0, Eq::Shape::bell, 1000.0f, -6.0f, -40.0f, 1.0f, 20.0f, 1.0f, 4.0f);
            start (from);
            play (from, sineRms (1000.0, -10.0, 24000));
            expectWithinAbsoluteError ((double) from.getLiveGainDb (0), -2.0, 0.05);
        }

        beginTest ("Attack and Release as set (time constants in dB)");
        {
            Eq eq;
            addDynamic (eq, 0, Eq::Shape::bell, 1000.0f, -12.0f, -50.0f, 50.0f, 200.0f);
            start (eq, sampleRate, 16);

            // 0.5 s of silence, 1 s loud (12 dB over: the target is -12, and it
            // leaves the cap as soon as the level falls), 1 s silence.
            std::vector<float> x (24000, 0.0f);
            const auto loud = sineRms (1000.0, -38.0, 48000);
            x.insert (x.end(), loud.begin(), loud.end());
            x.resize (x.size() + 48000, 0.0f);

            int attackAt = -1, releaseAt = -1;
            const auto attackLevel = -12.0 * (1.0 - std::exp (-1.0));   // 63 % of the way
            const auto releaseLevel = -12.0 * std::exp (-1.0);          // 63 % of the way back

            play (eq, x, 16, nullptr, [&] (int end)
            {
                const auto m = (double) eq.getDynamicGainDb (0);

                if (attackAt < 0 && end > 24000 && m <= attackLevel)
                    attackAt = end - 24000;

                if (releaseAt < 0 && end > 72000 && m >= releaseLevel)
                    releaseAt = end - 72000;
            });

            const auto attackMs = attackAt * 1000.0 / sampleRate, releaseMs = releaseAt * 1000.0 / sampleRate;
            logMessage ("Dynamic band, attack 50 ms / release 200 ms: measured " + juce::String (attackMs, 1) + " / "
                        + juce::String (releaseMs, 1) + " ms (the 5 ms detector adds a little)");
            expect (attackMs > 45.0 && attackMs < 65.0, "attack " + juce::String (attackMs));
            expect (releaseMs > 195.0 && releaseMs < 230.0, "release " + juce::String (releaseMs));
        }

        beginTest ("Sidechain: This band hears the EQ's input, Dry guitar the chain's input (This band without one)");
        {
            const auto quiet = sineRms (1000.0, -50.0, 24000);   // below the threshold
            const auto loud = sineRms (1000.0, -10.0, 24000);    // the dry guitar, well over

            const auto movement = [&] (Eq::Side side, const float* dry)
            {
                Eq eq;
                addDynamic (eq, 0, Eq::Shape::bell, 1000.0f, -8.0f, -30.0f);
                setSlot (eq, 0, "Side", (float) side);
                start (eq);
                play (eq, quiet, blockSize, dry);
                return (double) eq.getDynamicGainDb (0);
            };

            expectEquals (movement (Eq::Side::thisBand, loud.data()), 0.0, "This band: the input is quiet");
            expectWithinAbsoluteError (movement (Eq::Side::dryGuitar, loud.data()), -8.0, 0.01, "keyed by the dry guitar");
            expectEquals (movement (Eq::Side::dryGuitar, nullptr), 0.0, "no dry guitar: This band");

            // Bands don't steer each other: an earlier +20 dB bell doesn't push a later dynamic band over.
            Eq eq;
            setSlot (eq, 0, "Used", 1.0f);
            setSlot (eq, 0, "Gain", 20.0f);
            addDynamic (eq, 1, Eq::Shape::bell, 1000.0f, -8.0f, -40.0f);
            start (eq);
            play (eq, quiet);
            expectEquals ((double) eq.getDynamicGainDb (1), 0.0);
        }

        beginTest ("Sidechain filter: Follows the band (bell, shelves, tilt) or Own (SideFreq, SideQ)");
        {
            // A 5 kHz sine 10 dB over the threshold if heard unfiltered.
            const auto x = sineRms (5000.0, -20.0, 24000);

            const auto movement = [&] (Eq::Shape shape, float f, bool own, float sideFreq = 5000.0f)
            {
                Eq eq;
                addDynamic (eq, 0, shape, f, -12.0f, -30.0f);

                if (own)
                {
                    setSlot (eq, 0, "SideFilter", (float) Eq::SideFilter::own);
                    setSlot (eq, 0, "SideFreq", sideFreq);
                    setSlot (eq, 0, "SideQ", 1.0f);
                }

                start (eq);
                play (eq, x);
                return (double) eq.getDynamicGainDb (0);
            };

            // A bell at 1 kHz, Q 1, hears 5 kHz 13.8 dB down: under the knee.
            expectEquals (movement (Eq::Shape::bell, 1000.0f, false), 0.0);
            expectWithinAbsoluteError (movement (Eq::Shape::bell, 1000.0f, true), -10.0, 0.15, "Own at 5 kHz");
            expectEquals (movement (Eq::Shape::bell, 1000.0f, true, 500.0f), 0.0, "Own at 500 Hz");
            expectEquals (movement (Eq::Shape::lowShelf, 200.0f, false), 0.0, "a low shelf hears its lows");
            expectWithinAbsoluteError (movement (Eq::Shape::highShelf, 200.0f, false), -10.0, 0.15, "a high shelf hears its highs");
            expectWithinAbsoluteError (movement (Eq::Shape::tiltShelf, 200.0f, false), -10.0, 0.15, "a tilt hears everything");
        }

        beginTest ("Auto threshold: settles on plucks, frozen in silence and while tuning");
        {
            Eq eq;
            addDynamic (eq, 0, Eq::Shape::bell, 220.0f, -6.0f, -61.0f, 5.0f, 80.0f);   // Thresh Auto
            start (eq);
            expect (! std::isfinite (eq.getAutoThresholdDb (0)), "nothing learned yet");

            std::vector<float> notes;

            for (int k = 0; k < 12; ++k)
            {
                const auto note = pluck (24000, 0, 0.4f, 900.0, -80.0f, 110.0, 6, k + 1);
                notes.insert (notes.end(), note.signal.begin(), note.signal.end());
            }

            std::vector<float> at;
            play (eq, notes, blockSize, nullptr, [&] (int end) { if (end % 24000 < blockSize) at.push_back (eq.getAutoThresholdDb (0)); });
            const auto learned = (double) eq.getAutoThresholdDb (0);
            logMessage ("Auto threshold on plucks: " + juce::String (at[3], 1) + " dB at 2 s, " + juce::String (at[7], 1)
                        + " dB at 4 s, " + juce::String (learned, 1) + " dB at 6 s");
            expect (std::isfinite (learned));
            expectWithinAbsoluteError (learned, (double) at[7], 1.5, "settled");
            expect (eq.getDynamicGainDb (0) <= 0.0f);

            // It acts on the louder moments: the bell dips on a note's attack.
            auto dipped = false;
            play (eq, pluck (24000, 0, 0.4f, 900.0, -80.0f, 110.0, 6, 99).signal, blockSize, nullptr,
                  [&] (int) { dipped = dipped || eq.getDynamicGainDb (0) < -2.0f; });
            expect (dipped, "dips on the attack");

            // Silence: frozen.
            const auto before = eq.getAutoThresholdDb (0);
            play (eq, std::vector<float> (48000 * 3, 0.0f));
            expectEquals (eq.getAutoThresholdDb (0), before, "frozen in silence");
            expectEquals (eq.getDynamicGainDb (0), 0.0f, "back to rest");

            // Tuning, playing much louder: frozen.
            auto louder = notes;

            for (auto& v : louder)
                v *= 2.0f;

            play (eq, louder, blockSize, nullptr, {}, true);
            expectEquals (eq.getAutoThresholdDb (0), before, "frozen while tuning");

            // Tuner closed: it follows.
            play (eq, louder);
            expect (eq.getAutoThresholdDb (0) > before + 3.0f, "follows the louder playing");
        }

        beginTest ("No zipper noise while it moves (energy far above the signal)");
        {
            // A 300 Hz tone through a dynamic bell at 300 Hz, keyed by dry-guitar
            // bursts every 100 ms: the gain moves 12 dB in and out. (A 10 ms
            // attack: faster moves spread the tone's own sidebands up there.)
            Eq eq;
            addDynamic (eq, 0, Eq::Shape::bell, 300.0f, -12.0f, -30.0f, 10.0f, 30.0f);
            setSlot (eq, 0, "Side", (float) Eq::Side::dryGuitar);
            start (eq);
            const auto x = sine (300.0, 0.25f, 96000);
            std::vector<float> dry (96000, 0.0f);

            for (int i = 0; i < 96000; ++i)
                dry[(size_t) i] = (i / 4800) % 2 == 0 ? 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / sampleRate) : 0.0f;

            int lowest = 0;
            const auto y = play (eq, x, blockSize, dry.data(), [&] (int) { lowest = juce::jmin (lowest, juce::roundToInt (eq.getDynamicGainDb (0))); });
            expect (lowest <= -10, "it moved");
            const auto high = highBandDb (y, 6000.0, 4800, 96000);
            logMessage ("Dynamic band moving 12 dB every 100 ms: energy above 6 kHz " + juce::String (high, 1) + " dB");
            expect (high < -90.0, "no zipper noise: " + juce::String (high, 1));
        }

        beginTest ("Range 0 is bit-exact with the same band static; Mode on a cut is ignored; Spectral plays as Dynamic");
        {
            const auto x = noise (0.5f, 48000, 5);

            const auto render = [&] (Eq::Shape shape, Eq::Mode mode, float range)
            {
                Eq eq;
                addDynamic (eq, 0, shape, 1500.0f, range, -40.0f, 1.0f, 30.0f, 1.0f, 4.0f);
                setSlot (eq, 0, "Mode", (float) mode);
                start (eq);
                return play (eq, x);
            };

            expect (render (Eq::Shape::bell, Eq::Mode::dynamic, 0.0f) == render (Eq::Shape::bell, Eq::Mode::staticBand, -8.0f),
                    "Range 0 = static");
            expect (render (Eq::Shape::lowCut, Eq::Mode::dynamic, -8.0f) == render (Eq::Shape::lowCut, Eq::Mode::staticBand, -8.0f),
                    "a cut stays static");
            expect (render (Eq::Shape::notch, Eq::Mode::spectral, -8.0f) == render (Eq::Shape::notch, Eq::Mode::staticBand, -8.0f),
                    "a notch stays static");

            const auto dynamic = render (Eq::Shape::bell, Eq::Mode::dynamic, -8.0f);
            expect (dynamic != render (Eq::Shape::bell, Eq::Mode::staticBand, -8.0f), "dynamic moves");
            expect (dynamic == render (Eq::Shape::bell, Eq::Mode::spectral, -8.0f), "Spectral = Dynamic, bit for bit (until B5)");
        }

        beginTest ("Gain scale scales the movement; auto gain follows the set Gain, not the movement");
        {
            const auto x = sineRms (1000.0, -10.0, 24000);
            Eq half;
            addDynamic (half, 0, Eq::Shape::bell, 1000.0f, -8.0f, -40.0f);
            set (half, "gainScale", 50.0f);
            start (half);
            play (half, x);
            expectWithinAbsoluteError ((double) half.getDynamicGainDb (0), -4.0, 0.05);

            Eq curve;
            addDynamic (curve, 0, Eq::Shape::bell, 1000.0f, -8.0f, -40.0f, 1.0f, 30.0f, 1.0f, 3.0f);
            set (curve, "autoGain", (float) Eq::AutoGain::curve);
            start (curve);
            const auto settled = curve.getAutoGainDb();   // for the +3 dB bell at rest
            play (curve, x);
            expectWithinAbsoluteError ((double) curve.getDynamicGainDb (0), -8.0, 0.05);
            expectWithinAbsoluteError ((double) curve.getAutoGainDb(), (double) settled, 1.0e-6, "the movement doesn't move auto gain");
            expect (settled < -0.5f, "the set Gain does");
        }

        beginTest ("Placement on stereo: Left, Right, Mid and Side leave the rest untouched; Stereo does both");
        {
            const auto left = noise (0.5f, 24000, 11), right = noise (0.5f, 24000, 12);

            const auto render = [&] (Eq::Place place, std::vector<float>& l, std::vector<float>& r)
            {
                Eq eq;
                setSlot (eq, 0, "Used", 1.0f);
                setSlot (eq, 0, "Gain", 12.0f);
                setSlot (eq, 0, "Place", (float) place);
                start (eq);
                l = left;
                r = right;

                for (int at = 0; at < 24000; at += blockSize)
                {
                    float* ch[] = { l.data() + at, r.data() + at };
                    eq.process (ch, 2, juce::jmin (blockSize, 24000 - at), {});
                }
            };

            const auto midSideError = [&] (const std::vector<float>& l, const std::vector<float>& r, bool mid)
            {
                auto worst = 0.0;

                for (size_t i = 0; i < l.size(); ++i)
                {
                    const auto in = mid ? 0.5 * ((double) left[i] + right[i]) : 0.5 * ((double) left[i] - right[i]);
                    const auto out = mid ? 0.5 * ((double) l[i] + r[i]) : 0.5 * ((double) l[i] - r[i]);
                    worst = std::max (worst, std::abs (out - in));
                }

                return worst;
            };

            std::vector<float> l, r;
            render (Eq::Place::left, l, r);
            expect (r == right && l != left, "Left: the right channel bit-exact");
            render (Eq::Place::right, l, r);
            expect (l == left && r != right, "Right: the left channel bit-exact");
            render (Eq::Place::mid, l, r);
            expect (midSideError (l, r, false) < 1.0e-6 && midSideError (l, r, true) > 0.01, "Mid: S untouched (to float precision)");
            render (Eq::Place::side, l, r);
            expect (midSideError (l, r, true) < 1.0e-6 && midSideError (l, r, false) > 0.01, "Side: M untouched (to float precision)");
            render (Eq::Place::stereo, l, r);
            expect (l != left && r != right);

            // A band that changes nothing makes no M/S round trip: bit-exact.
            Eq flat;
            setSlot (flat, 0, "Used", 1.0f);
            setSlot (flat, 0, "Place", (float) Eq::Place::mid);
            start (flat);
            l = left;
            r = right;
            float* ch[] = { l.data(), r.data() };
            flat.process (ch, 2, blockSize, {});
            expect (std::equal (l.begin(), l.begin() + blockSize, left.begin()) && std::equal (r.begin(), r.begin() + blockSize, right.begin()));
        }

        beginTest ("Placement on mono is ignored (plays as Stereo); a Place change crossfades that band, no click");
        {
            const auto x = noise (0.5f, 24000, 13);

            const auto mono = [&] (Eq::Place place)
            {
                Eq eq;
                setSlot (eq, 0, "Used", 1.0f);
                setSlot (eq, 0, "Gain", 9.0f);
                setSlot (eq, 0, "Place", (float) place);
                start (eq);
                return play (eq, x);
            };

            const auto stereo = mono (Eq::Place::stereo);

            for (auto place : { Eq::Place::left, Eq::Place::right, Eq::Place::mid, Eq::Place::side })
                expect (mono (place) == stereo, "mono ignores Place " + juce::String ((int) place));

            // Left -> Right on a low tone while playing: the change crossfades.
            Eq eq;
            setSlot (eq, 0, "Used", 1.0f);
            setSlot (eq, 0, "Freq", 200.0f);
            setSlot (eq, 0, "Gain", 12.0f);
            setSlot (eq, 0, "Place", (float) Eq::Place::left);
            start (eq);
            auto l = sine (200.0, 0.2f, 24000), r = l;

            for (int at = 0; at < 24000; at += blockSize)
            {
                if (at == 12032)
                    setSlot (eq, 0, "Place", (float) Eq::Place::right);

                float* ch[] = { l.data() + at, r.data() + at };
                eq.process (ch, 2, juce::jmin (blockSize, 24000 - at), {});
            }

            const auto clickL = highBandDb (l, 3000.0, 4800, 24000), clickR = highBandDb (r, 3000.0, 4800, 24000);
            logMessage ("Place Left -> Right on a 200 Hz tone: energy above 3 kHz " + juce::String (clickL, 1) + " / "
                        + juce::String (clickR, 1) + " dB");
            expect (clickL < -70.0 && clickR < -70.0, "no click");
            expectWithinAbsoluteError ((double) juce::Decibels::gainToDecibels (rms (r, 20000, 24000) / rms (l, 20000, 24000)), 12.0, 0.1,
                                       "the band moved to the right");
        }

        beginTest ("A dynamic band's detector follows its placement: Mid hears M, Side hears S");
        {
            const auto tone = sineRms (1000.0, -10.0, 24000);
            auto inverted = tone;

            for (auto& v : inverted)
                v = -v;

            const auto movement = [&] (Eq::Place place, const std::vector<float>& right, Eq::Side side = Eq::Side::thisBand)
            {
                Eq eq;
                addDynamic (eq, 0, Eq::Shape::bell, 1000.0f, -8.0f, -30.0f);
                setSlot (eq, 0, "Place", (float) place);
                setSlot (eq, 0, "Side", (float) side);
                start (eq);
                auto l = tone, r = right;
                const auto dry = tone;

                for (int at = 0; at < 24000; at += blockSize)
                {
                    float* ch[] = { l.data() + at, r.data() + at };
                    ProcessContext context;
                    context.chainInput = dry.data() + at;
                    eq.process (ch, 2, juce::jmin (blockSize, 24000 - at), context);
                }

                return (double) eq.getDynamicGainDb (0);
            };

            // L = R: all mid.  L = -R: all side.
            expectWithinAbsoluteError (movement (Eq::Place::mid, tone), -8.0, 0.01);
            expectEquals (movement (Eq::Place::side, tone), 0.0);
            expectEquals (movement (Eq::Place::mid, inverted), 0.0);
            expectWithinAbsoluteError (movement (Eq::Place::side, inverted), -8.0, 0.01);
            expectWithinAbsoluteError (movement (Eq::Place::stereo, inverted), -8.0, 0.01, "Stereo: the louder channel");

            // Left hears the left alone.
            std::vector<float> silent (24000, 0.0f);
            expectWithinAbsoluteError (movement (Eq::Place::left, silent), -8.0, 0.01);
            expectEquals (movement (Eq::Place::right, silent), 0.0);

            // From the dry guitar (mono) every placement hears the guitar.
            expectWithinAbsoluteError (movement (Eq::Place::side, tone, Eq::Side::dryGuitar), -8.0, 0.01);
        }

        beginTest ("24 dynamic bands moving, stereo: no allocation, no NaN");
        {
            Eq eq;

            for (int s = 0; s < Eq::numSlots; ++s)
            {
                addDynamic (eq, s, (Eq::Shape) ((s % 3) == 0 ? 0 : (s % 3) == 1 ? 1 : 7), 50.0f + 800.0f * s, s % 2 ? -30.0f : 30.0f,
                            s % 4 == 0 ? -61.0f : -40.0f, 0.1f, 5.0f, s % 2 ? 40.0f : 0.025f);
                setSlot (eq, s, "Side", (float) (s % 2));
                setSlot (eq, s, "SideFilter", (float) (s % 3 == 0));
                setSlot (eq, s, "Place", (float) (s % 5));
            }

            start (eq);
            auto left = noise (0.9f, 48000, 3), right = noise (0.9f, 48000, 4);
            const auto dry = noise (0.5f, 48000, 6);
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int at = 0; at < 48000; at += blockSize)
                {
                    float* ch[] = { left.data() + at, right.data() + at };
                    ProcessContext context;
                    context.chainInput = dry.data() + at;
                    eq.process (ch, 2, blockSize, context);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expect (std::all_of (left.begin(), left.end(), [] (float v) { return std::isfinite (v); }));
            expect (std::all_of (right.begin(), right.end(), [] (float v) { return std::isfinite (v); }));
        }
    }
};

static StudioEqDynamicsTests studioEqDynamicsTests;

} // namespace encore::test
