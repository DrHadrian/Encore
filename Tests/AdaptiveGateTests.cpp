// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2: the adaptive ("Smart") gate shared by the Pedal
// Gate "S2" and the Studio Gate "X4" (GATE-PLAN 13, 23).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/core/Realtime.h"
#include "engine/dsp/AdaptiveGate.h"

namespace encore::test
{

class AdaptiveGateTests final : public juce::UnitTest
{
public:
    AdaptiveGateTests() : juce::UnitTest ("Adaptive gate", "Encore") {}

    struct Run
    {
        std::vector<float> gains;
        int opens = 0, closes = 0;
        int firstClose = -1;
        float floorDb = 0.0f;
        bool frozen = false;
    };

    static Run run (dsp::AdaptiveGate& gate, const std::vector<float>& signal, bool tunerOpen = false,
                    int countFrom = 0, double rate = sampleRate)
    {
        dsp::LevelDetector detector;
        detector.prepare (rate);
        detector.setTimeMs (10.0);

        Run r;
        auto wasOpen = gate.isOpen();

        for (size_t i = 0; i < signal.size(); ++i)
        {
            r.gains.push_back (gate.process (detector.process (signal[i]), tunerOpen));

            if (gate.isOpen() != wasOpen && (int) i >= countFrom)
            {
                (gate.isOpen() ? r.opens : r.closes)++;

                if (! gate.isOpen() && r.firstClose < 0)
                    r.firstClose = (int) i;
            }

            wasOpen = gate.isOpen();
        }

        r.floorDb = gate.getFloorDb();
        r.frozen = gate.isFrozen();
        return r;
    }

    static void setUp (dsp::AdaptiveGate& gate, double rate = sampleRate, dsp::AdaptiveGateSettings settings = {})
    {
        gate.prepare (rate);
        gate.setSettings (settings);
        gate.reset (true);
    }

    /** Samples from the first close until the gain is 60 dB down. */
    static int releaseLength (const Run& r)
    {
        if (r.firstClose < 0)
            return -1;

        for (auto i = (size_t) r.firstClose; i < r.gains.size(); ++i)
            if (r.gains[i] <= 0.001f)
                return (int) i - r.firstClose;

        return -1;
    }

    void runTest() override
    {
        beginTest ("Before anything is learned the floor is -70 dBFS");
        {
            dsp::AdaptiveGate gate;
            setUp (gate);
            expectEquals (gate.getFloorDb(), dsp::AdaptiveGate::initialFloorDb);
            expectWithinAbsoluteError (gate.getThresholdDb(), -61.0f, 1.0e-4f, "the floor plus the offset (9 dB)");
            expect (gate.isOpen(), "starts open, as today's gate");
        }

        beginTest ("The floor learns the noise: within 3 dB of a known floor, from -70, at every rate");
        {
            for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                for (auto floorDb : { -52.0f, -60.0f, -78.0f })
                {
                    dsp::AdaptiveGate gate;
            setUp (gate, rate);
                    // 6 s of noise alone, then a pluck that decays back into it.
                    const auto n = (int) (10.0 * rate);
                    auto note = pluck (n, (int) (6.0 * rate), 0.5f, 2000.0, floorDb, 110.0, 6, 3, rate);
                    const auto r = run (gate, note.signal, false, 0, rate);
                    expectWithinAbsoluteError (r.floorDb, floorDb, 3.0f,
                                               "noise at " + juce::String (floorDb) + " dB, " + juce::String (rate) + " Hz");
                }
            }

            // Palm-mute chugs over the noise: still the noise's level.
            dsp::AdaptiveGate gate;
            setUp (gate);
            std::vector<float> chugs;

            for (int k = 0; k < 16; ++k)
            {
                auto chug = palmMute (k == 0 ? 6 * 48000 : 9600, k == 0 ? 6 * 48000 - 9600 : 0, 0.5f, -56.0f, k + 1);
                chugs.insert (chugs.end(), chug.signal.begin(), chug.signal.end());
            }

            const auto r = run (gate, chugs);
            expectWithinAbsoluteError (r.floorDb, -56.0f, 3.0f, "after chugs");
            logMessage ("Learned " + juce::String (r.floorDb, 1) + " dB from noise at -56 dB with chugs");
        }

        beginTest ("The floor moves at most 6 dB per second");
        {
            dsp::AdaptiveGate gate;
            setUp (gate);
            const auto r = run (gate, noise (juce::Decibels::decibelsToGain (-45.0f), 48000 * 2));
            // 2 s: 0.5 s before learning starts, then at most 9 dB in the remaining 1.5 s.
            expect (r.floorDb <= -70.0f + 9.0f + 0.1f, "slew limited: " + juce::String (r.floorDb));
            expect (r.floorDb > -70.0f + 6.0f, "but moving: " + juce::String (r.floorDb));
        }

        beginTest ("Near-silence and an open tuner leave the floor unchanged, and say they're frozen");
        {
            dsp::AdaptiveGate quiet;
            setUp (quiet);
            const auto r = run (quiet, noise (juce::Decibels::decibelsToGain (-90.0f), 48000 * 4));
            expectEquals (r.floorDb, dsp::AdaptiveGate::initialFloorDb, "cable out, volume at 0");
            expect (r.frozen);

            dsp::AdaptiveGate tuning;
            setUp (tuning);
            const auto t = run (tuning, noise (juce::Decibels::decibelsToGain (-50.0f), 48000 * 4), true);
            expectEquals (t.floorDb, dsp::AdaptiveGate::initialFloorDb, "the tuner is open");
            expect (t.frozen);

            dsp::AdaptiveGate after;
            setUp (after);
            run (after, noise (juce::Decibels::decibelsToGain (-50.0f), 48000 * 2), true);
            const auto a = run (after, noise (juce::Decibels::decibelsToGain (-50.0f), 48000 * 6));
            expect (! a.frozen, "learning again once the tuner closes");
            expectWithinAbsoluteError (a.floorDb, -50.0f, 3.0f);
        }

        beginTest ("A sustained note isn't learned as noise");
        {
            dsp::AdaptiveGate gate;
            setUp (gate);
            gate.setFloorDb (-70.0f);
            // A held note 2 s long (to -60 dB), right away: nothing steady for 500 ms.
            auto note = pluck (48000 * 3, 480, 0.5f, 2000.0, -70.0f);
            const auto r = run (gate, std::vector<float> (note.signal.begin(), note.signal.begin() + 48000 * 2));
            expectWithinAbsoluteError (r.floorDb, -70.0f, 0.5f, "the floor didn't follow the note: " + juce::String (r.floorDb));
        }

        beginTest ("The release follows the note: palm-mute chugs close faster than a held chord");
        {
            dsp::AdaptiveGate chordGate;
            setUp (chordGate);
            chordGate.setFloorDb (-70.0f);
            auto chord = pluck (48000 * 8, 4800, 0.5f, 6000.0, -70.0f);   // rings 6 s
            const auto c = run (chordGate, chord.signal, false, 4800);

            dsp::AdaptiveGate chugGate;
            setUp (chugGate);
            chugGate.setFloorDb (-70.0f);
            auto chug = palmMute (48000 * 2, 4800, 0.5f, -70.0f);
            const auto p = run (chugGate, chug.signal, false, 4800);

            const auto chordRelease = releaseLength (c), chugRelease = releaseLength (p);
            logMessage ("Release (to -60 dB) after a held chord " + juce::String (chordRelease / 48.0, 1)
                        + " ms, after a palm-mute " + juce::String (chugRelease / 48.0, 1) + " ms (base 87 ms)");
            expect (chordRelease > 0 && chugRelease > 0);
            expect (chugRelease * 4 < chordRelease, "a palm-mute closes much faster");
            expectWithinAbsoluteError (dsp::AdaptiveGate::releaseFactorFor (5.0f), 2.5f, 1.0e-6f);
            expectWithinAbsoluteError (dsp::AdaptiveGate::releaseFactorFor (500.0f), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (dsp::AdaptiveGate::releaseFactorFor (std::sqrt (20.0f * 200.0f)),
                                       std::sqrt (2.5f * 0.25f), 1.0e-4f, "log-interpolated between");
        }

        beginTest ("A slowly decaying note doesn't chatter: the chatter guard holds instead");
        {
            // A note through a hard tremolo (30 ms on, 30 ms off), decaying slowly
            // around the threshold: each gap outlasts the 20 ms hold and the
            // next pulse comes within 50 ms, so a plain gate flutters on every
            // pulse.
            const int n = 48000 * 4;
            std::vector<float> beating ((size_t) n);
            auto rough = noise (juce::Decibels::decibelsToGain (-70.0f), n, 7);

            for (int i = 0; i < n; ++i)
            {
                const auto t = i / sampleRate;
                const auto env = std::pow (10.0, (-50.0 - 3.0 * t) / 20.0);
                const auto on = std::fmod (t, 0.060) < 0.030;
                beating[(size_t) i] = (float) (on ? env * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) : 0.0)
                                      + rough[(size_t) i];
            }

            dsp::AdaptiveGate guarded;
            setUp (guarded);
            guarded.setFloorDb (-70.0f);
            const auto g = run (guarded, beating);

            dsp::AdaptiveGateSettings plain;
            plain.chatterGuard = false;
            dsp::AdaptiveGate unguarded;
            setUp (unguarded, sampleRate, plain);
            unguarded.setFloorDb (-70.0f);
            const auto u = run (unguarded, beating);

            logMessage ("Open/close changes on a tremolo decay: " + juce::String (g.opens + g.closes)
                        + " with the chatter guard, " + juce::String (u.opens + u.closes) + " without");
            expect (u.opens + u.closes > 20, "the signal does provoke chatter");
            expect (g.opens + g.closes <= 4, "the guard holds it open through the gaps");
            expect ((g.opens + g.closes) * 5 < u.opens + u.closes, "it holds instead");
        }

        beginTest ("Keeps the pick attack: the onset peak is within 1 dB of the input's");
        {
            for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                dsp::AdaptiveGate gate;
            setUp (gate, rate);
                gate.setFloorDb (-70.0f);
                const auto onset = (int) rate;   // 1 s of noise: closed by then
                auto note = pluck ((int) (rate * 1.5), onset, 0.5f, 2000.0, -70.0f, 110.0, 6, 1, rate);
                const auto r = run (gate, note.signal, false, 0, rate);

                expect (r.gains[(size_t) onset - 1] < 0.01f, "closed before the note");

                std::vector<float> out (note.signal.size());

                for (size_t i = 0; i < out.size(); ++i)
                    out[i] = note.signal[i] * r.gains[i];

                const auto window = (int) (0.005 * rate);
                const auto inPeak = peak (note.signal, onset, onset + window);
                const auto outPeak = peak (out, onset, onset + window);
                expectWithinAbsoluteError (toDb (outPeak), toDb (inPeak), 1.0f, "at " + juce::String (rate));
            }
        }

        beginTest ("A follower driven by the leader's decision gives the same gain");
        {
            dsp::AdaptiveGate leader;
            setUp (leader);
            dsp::AdaptiveGate follower;
            setUp (follower);
            leader.setFloorDb (-70.0f);
            follower.setSharedThresholdDb (leader.getThresholdDb());

            dsp::LevelDetector detector;
            detector.prepare (sampleRate);
            auto note = palmMute (48000 * 2, 4800, 0.5f, -70.0f);
            auto maxDifference = 0.0f;

            for (auto x : note.signal)
            {
                const auto level = detector.process (x);
                const auto a = leader.process (level, true);   // frozen: the threshold stays put
                const auto b = follower.processWithDecision (leader.isOpen(), level);
                maxDifference = juce::jmax (maxDifference, std::abs (a - b));
            }

            expectEquals (maxDifference, 0.0f);
        }

        beginTest ("Soft: an expander 1:2, at most 15 dB down");
        {
            dsp::AdaptiveGateSettings soft;
            soft.mode = dsp::GateMode::expander;
            soft.ratio = 2.0f;
            soft.rangeDb = -15.0f;
            dsp::AdaptiveGate gate;
            setUp (gate, sampleRate, soft);
            gate.setFloorDb (-70.0f);   // threshold -61
            const auto r = run (gate, std::vector<float> (48000, 0.0005f), true);   // -66 dB, frozen
            expectWithinAbsoluteError (-toDb (r.gains.back()), 5.0f, 0.05f, "5 dB under: 5 dB down");
            const auto q = run (gate, std::vector<float> (48000, 0.00001f), true);
            expectWithinAbsoluteError (-toDb (q.gains.back()), 15.0f, 0.01f, "capped at 15");
        }

        beginTest ("No allocation, no NaN at extreme input");
        {
            dsp::AdaptiveGate gate;
            setUp (gate);
            dsp::LevelDetector detector;
            detector.prepare (sampleRate);
            auto allFinite = true;
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 48000; ++i)
                {
                    const auto x = i % 997 == 0 ? 1.0e6f : (i % 3 == 0 ? 0.0f : 1.0e-30f);
                    allFinite = allFinite && std::isfinite (gate.process (detector.process (x), i % 5000 < 100));
                    allFinite = allFinite && std::isfinite (gate.processWithDecision (i % 7 == 0, 0.0f));
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expect (allFinite);
            expect (gate.getFloorDb() >= dsp::AdaptiveGate::minFloorDb && gate.getFloorDb() <= dsp::AdaptiveGate::maxFloorDb);
        }
    }
};

static AdaptiveGateTests adaptiveGateTests;

} // namespace encore::test
