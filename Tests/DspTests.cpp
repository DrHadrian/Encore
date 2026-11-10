// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "TestSignals.h"

#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/dsp/Biquad.h"
#include "engine/dsp/SafetyLimiter.h"

namespace encore::test
{

class BiquadTests final : public juce::UnitTest
{
public:
    BiquadTests() : juce::UnitTest ("Biquad responses", "Encore") {}

    void runTest() override
    {
        using C = dsp::BiquadCoefficients;
        const auto db = [] (double g) { return 20.0 * std::log10 (g); };

        beginTest ("Low shelf +6 dB lifts lows, leaves highs");
        {
            const auto c = C::lowShelf (sampleRate, 120.0, 6.0);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 20.0)), 6.0, 0.3);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 10000.0)), 0.0, 0.1);
        }

        beginTest ("High shelf -6 dB cuts highs, leaves lows");
        {
            const auto c = C::highShelf (sampleRate, 3500.0, -6.0);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 18000.0)), -6.0, 0.4);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 100.0)), 0.0, 0.1);
        }

        beginTest ("Peak gives its gain at the centre frequency");
        {
            const auto c = C::peak (sampleRate, 1000.0, 0.9, 9.0);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 1000.0)), 9.0, 0.05);
        }

        beginTest ("High-pass: -3 dB at the corner, blocks lows");
        {
            const auto c = C::highPass (sampleRate, 200.0, 0.7071);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 200.0)), -3.0, 0.1);
            expectLessThan (db (c.magnitudeAt (sampleRate, 20.0)), -35.0);
            expectWithinAbsoluteError (db (c.magnitudeAt (sampleRate, 5000.0)), 0.0, 0.1);
        }

        beginTest ("Corners above Nyquist are clamped, filter stays stable");
        {
            dsp::Biquad<1> f;
            f.setCoefficients (C::lowPass (44100.0, 30000.0, 0.7071));
            auto signal = noise (1.0f, 44100);
            f.process (signal.data(), 0, (int) signal.size());

            for (auto x : signal)
                expect (std::isfinite (x));
        }
    }
};

class EqBlockTests final : public juce::UnitTest
{
public:
    EqBlockTests() : juce::UnitTest ("EQ block", "Encore") {}

    void runTest() override
    {
        beginTest ("Default settings are bit-exact passthrough");
        {
            EqBlock eq;
            eq.prepare (spec);
            const auto input = noise (0.5f, 4800);
            const auto output = runMono (eq, input);
            expect (input == output);
        }

        beginTest ("Mid +6 dB at 1 kHz boosts a 1 kHz tone by 6 dB");
        {
            EqBlock eq;
            eq.prepare (spec);
            eq.findParameter ("mid")->setValue (6.0f);
            eq.findParameter ("midFreq")->setValue (1000.0f);

            const auto input = sine (1000.0, 0.1f, 48000);
            const auto output = runMono (eq, input);   // ramps settle well within 0.5 s
            expectWithinAbsoluteError (toDb (rms (output, 24000, 48000) / rms (input, 24000, 48000)), 6.0f, 0.2f);
        }

        beginTest ("Sweeping a knob while playing stays finite and bounded");
        {
            EqBlock eq;
            eq.prepare (spec);
            auto signal = sine (440.0, 0.5f, 48000);

            for (int offset = 0; offset < (int) signal.size(); offset += blockSize)
            {
                eq.findParameter ("midFreq")->setValue (200.0f + (float) (offset % 4800));
                eq.findParameter ("bass")->setValue ((offset / blockSize) % 2 == 0 ? 12.0f : -12.0f);
                float* ch[1] = { signal.data() + offset };
                eq.process (ch, 1, blockSize);
            }

            expectLessThan (peak (signal, 0, (int) signal.size()), 3.0f);

            for (auto x : signal)
                expect (std::isfinite (x));
        }

        beginTest ("Bypass returns the dry signal exactly once the fade is done");
        {
            EqBlock eq;
            eq.prepare (spec);
            eq.findParameter ("treble")->setValue (12.0f);
            runMono (eq, noise (0.5f, 4800));          // let it settle while on

            eq.setEnabled (false);
            runMono (eq, noise (0.5f, 4800, 2));       // 100 ms: fade (10 ms) completes

            const auto input = noise (0.5f, 4800, 3);
            expect (runMono (eq, input) == input);
            expectEquals (eq.getCpuLoad(), 0.0, "a sleeping block costs nothing");
        }
    }
};

class NoiseGateTests final : public juce::UnitTest
{
public:
    NoiseGateTests() : juce::UnitTest ("Noise gate", "Encore") {}

    void runTest() override
    {
        beginTest ("Hiss below the threshold is silenced");
        {
            NoiseGateBlock gate;
            gate.prepare (spec);
            gate.findParameter ("threshold")->setValue (-40.0f);

            const auto hiss = noise (juce::Decibels::decibelsToGain (-60.0f), 96000);
            const auto output = runMono (gate, hiss);
            expectLessThan (toDb (rms (output, 72000, 96000)), -120.0f);
            expect (! gate.isOpen());
        }

        beginTest ("A note above the threshold passes untouched");
        {
            NoiseGateBlock gate;
            gate.prepare (spec);
            gate.findParameter ("threshold")->setValue (-40.0f);

            const auto note = sine (220.0, juce::Decibels::decibelsToGain (-12.0f), 48000);
            const auto output = runMono (gate, note);
            expectWithinAbsoluteError (toDb (rms (output, 24000, 48000) / rms (note, 24000, 48000)), 0.0f, 0.01f);
            expect (gate.isOpen());
        }

        beginTest ("Range sets how far the gate closes");
        {
            NoiseGateBlock gate;
            gate.prepare (spec);
            gate.findParameter ("threshold")->setValue (-40.0f);
            gate.findParameter ("range")->setValue (-20.0f);

            const auto hiss = noise (juce::Decibels::decibelsToGain (-60.0f), 96000);
            const auto output = runMono (gate, hiss);
            expectWithinAbsoluteError (toDb (rms (output, 72000, 96000) / rms (hiss, 72000, 96000)), -20.0f, 0.5f);
        }
    }
};

class SafetyLimiterTests final : public juce::UnitTest
{
public:
    SafetyLimiterTests() : juce::UnitTest ("Safety limiter", "Encore") {}

    void runTest() override
    {
        const auto ceiling = juce::Decibels::decibelsToGain (dsp::SafetyLimiter::ceilingDb);

        beginTest ("Nothing gets past the ceiling, even +20 dB over");
        {
            dsp::SafetyLimiter limiter;
            limiter.prepare (sampleRate);
            const auto output = runMono (limiter, sine (100.0, 10.0f, 48000));
            expectLessOrEqual (peak (output, 0, 48000), ceiling + 1.0e-6f);
            expectLessThan (limiter.takeGainReductionDb(), -15.0f);
        }

        beginTest ("Signals below the ceiling are bit-exact");
        {
            dsp::SafetyLimiter limiter;
            limiter.prepare (sampleRate);
            const auto input = sine (440.0, 0.5f, 48000);
            expect (runMono (limiter, input) == input);
            expectEquals (limiter.takeGainReductionDb(), 0.0f);
        }

        beginTest ("Gain recovers after a spike");
        {
            dsp::SafetyLimiter limiter;
            limiter.prepare (sampleRate);
            runMono (limiter, sine (100.0, 10.0f, 4800));
            const auto after = runMono (limiter, sine (440.0, 0.5f, 96000));
            expectWithinAbsoluteError (rms (after, 72000, 96000), rms (sine (440.0, 0.5f, 96000), 72000, 96000), 0.001f);
        }
    }
};

static BiquadTests biquadTests;
static EqBlockTests eqBlockTests;
static NoiseGateTests noiseGateTests;
static SafetyLimiterTests safetyLimiterTests;

} // namespace encore::test
