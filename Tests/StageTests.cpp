// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "FakeDevice.h"
#include "TestSignals.h"

#include "engine/AudioEngine.h"
#include "engine/blocks/DelayBlock.h"
#include "engine/blocks/EqBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    /** A plucked-string-like tone: strong harmonics (the 2nd louder than the
        fundamental, as on a guitar's low strings) with a slow decay. */
    std::vector<float> guitarNote (double frequency, int numSamples)
    {
        std::vector<float> s ((size_t) numSamples);
        const float weights[] = { 0.6f, 1.0f, 0.5f, 0.35f, 0.2f, 0.12f };

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = i / sampleRate;
            double x = 0.0;

            for (int h = 0; h < 6; ++h)
                x += weights[h] * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * (h + 1) * t + h);

            s[(size_t) i] = (float) (0.2 * x * std::exp (-t * 0.8));
        }

        return s;
    }

    double detuned (double frequency, double cents) { return frequency * std::pow (2.0, cents / 1200.0); }
}

class StageTests final : public juce::UnitTest
{
public:
    StageTests() : juce::UnitTest ("Tuner and panic", "Encore") {}

    static Tuner::Reading tune (Tuner& tuner, const std::vector<float>& signal)
    {
        tuner.setActive (true);

        for (size_t i = 0; i + blockSize <= signal.size(); i += blockSize)
            tuner.push (signal.data() + i, blockSize);

        Tuner::Reading reading;

        for (int i = 0; i < 20; ++i)   // a second of readings, as the screen takes them, to settle the needle
            reading = tuner.analyse();

        return reading;
    }

    /** An engine playing one chain, fed with a signal block by block. */
    struct Rig
    {
        FakeDevice device;
        AudioEngine engine;
        std::vector<float> outL = std::vector<float> ((size_t) blockSize), outR = outL;

        explicit Rig (std::unique_ptr<Chain> chain)
        {
            engine.audioDeviceAboutToStart (&device);
            engine.setMasterGainDecibels (0.0f);
            engine.setMuted (false);
            engine.setChain (std::move (chain));
        }

        ~Rig() { engine.audioDeviceStopped(); }

        /** Plays `numSamples` of `input` (repeated if shorter), returns the output. */
        std::vector<float> play (const std::vector<float>& input, int numSamples)
        {
            std::vector<float> result;
            std::vector<float> block ((size_t) blockSize);

            for (int done = 0; done < numSamples; done += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                    block[(size_t) i] = input.empty() ? 0.0f : input[(size_t) (done + i) % input.size()];

                const float* inputs[] = { block.data() };
                float* outputs[] = { outL.data(), outR.data() };
                engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, {});
                result.insert (result.end(), outL.begin(), outL.end());
            }

            return result;
        }
    };

    static std::unique_ptr<Chain> passThrough()
    {
        auto chain = std::make_unique<Chain>();
        chain->addBlock (std::make_unique<EqBlock>());
        return chain;
    }

    void runTest() override
    {
        beginTest ("The tuner finds every open string, standard and dropped");
        {
            Tuner tuner;
            tuner.prepare (sampleRate);

            const std::pair<double, int> strings[] = { { 82.41, 40 }, { 110.0, 45 }, { 146.83, 50 }, { 196.0, 55 },
                                                       { 246.94, 59 }, { 329.63, 64 }, { 73.42, 38 }, { 61.74, 35 },
                                                       { 41.2, 28 }, { 1318.5, 88 } };

            for (const auto& [frequency, note] : strings)
            {
                const auto reading = tune (tuner, guitarNote (frequency, 24000));
                expect (reading.valid, juce::String (frequency) + " Hz detected");
                expectEquals (reading.midiNote, note, juce::String (frequency) + " Hz is the right note");
                expect (std::abs (reading.cents) < 1.5f, juce::String (frequency) + " Hz reads in tune: "
                                                          + juce::String (reading.cents, 2));
            }
        }

        beginTest ("The needle shows cents sharp and flat");
        {
            Tuner tuner;
            tuner.prepare (sampleRate);

            for (const auto cents : { -30.0, -10.0, -3.0, 4.0, 15.0, 40.0 })
            {
                const auto reading = tune (tuner, guitarNote (detuned (110.0, cents), 24000));
                expectEquals (reading.midiNote, 45);
                expectWithinAbsoluteError (reading.cents, (float) cents, 1.5f);
            }

            const auto e = tune (tuner, guitarNote (329.63, 24000));
            expectEquals (e.getNoteName(), juce::String ("E"));
            expectEquals (e.getOctave(), 4);
            expect (e.isInTune());
        }

        beginTest ("Concert pitch can be changed");
        {
            Tuner tuner;
            tuner.prepare (sampleRate);
            tuner.setReferencePitch (432.0f);
            const auto reading = tune (tuner, sine (432.0, 0.3f, 24000));
            expectEquals (reading.midiNote, 69);
            expectWithinAbsoluteError (reading.cents, 0.0f, 1.0f);
        }

        beginTest ("No note, no reading; a closed tuner takes nothing");
        {
            Tuner tuner;
            tuner.prepare (sampleRate);
            expect (! tune (tuner, std::vector<float> (24000, 0.0f)).valid, "silence");
            expect (! tune (tuner, noise (0.2f, 24000)).valid, "noise has no pitch");

            Tuner closed;
            closed.prepare (sampleRate);
            const auto signal = sine (110.0, 0.3f, 4800);
            closed.push (signal.data(), (int) signal.size());
            expect (! closed.analyse().valid);
        }

        beginTest ("Other sample rates");
        {
            for (const auto rate : { 44100.0, 96000.0 })
            {
                Tuner tuner;
                tuner.prepare (rate);
                tuner.setActive (true);

                std::vector<float> s (48000);

                for (size_t i = 0; i < s.size(); ++i)
                    s[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * (double) i / rate);

                for (size_t i = 0; i + 128 <= s.size(); i += 128)
                    tuner.push (s.data() + i, 128);

                const auto reading = tuner.analyse();
                expectEquals (reading.midiNote, 55, juce::String (rate));
                expectWithinAbsoluteError (reading.cents, 0.0f, 1.5f);
            }
        }

        beginTest ("An open tuner mutes the output, unless set to play through");
        {
            Rig rig (passThrough());
            const auto tone = sine (110.0, 0.3f, 4800);
            expect (rms (rig.play (tone, 4800), 2400, 4800) > 0.1f, "playing");

            rig.engine.setTunerOpen (true);
            expect (peak (rig.play (tone, 9600), 2400, 9600) < 1.0e-6f, "muted while tuning");
            expect (rig.engine.getTuner().analyse().midiNote == 45, "the tuner hears it");

            rig.engine.setTunerMutes (false);
            expect (rms (rig.play (tone, 4800), 2400, 4800) > 0.1f, "plays through when asked");

            rig.engine.setTunerMutes (true);
            rig.engine.setTunerOpen (false);
            expect (rms (rig.play (tone, 4800), 2400, 4800) > 0.1f, "back after closing");
        }

        beginTest ("Panic silences at once, clears tails, and comes back by itself");
        {
            auto chain = std::make_unique<Chain>();
            auto& delay = chain->addBlock (std::make_unique<DelayBlock>());
            delay.findParameter ("feedback")->setValue (90.0f);
            delay.findParameter ("mix")->setValue (100.0f);
            delay.findParameter ("time")->setValue (100.0f);
            Rig rig (std::move (chain));

            // A burst, then silence: the delay keeps repeating it.
            rig.play (sine (440.0, 0.3f, 2400), 2400);
            expect (rms (rig.play ({}, 9600), 0, 9600) > 0.01f, "repeats ringing");

            rig.engine.panic();
            const auto during = rig.play ({}, 960);   // 20 ms
            expect (peak (during, 480, 960) < 1.0e-6f, "silent within 10 ms");
            expect (rig.engine.getPanicState() == AudioEngine::PanicState::silent);

            // Comes back after hold + return, with the repeats gone.
            const auto after = rig.play ({}, (int) ((AudioEngine::panicHoldSeconds + AudioEngine::panicReturnSeconds + 0.1) * sampleRate));
            expect (rig.engine.getPanicState() == AudioEngine::PanicState::none, "recovered");
            expect (peak (after, 0, (int) after.size()) < 1.0e-6f, "the repeats were cleared");

            const auto tone = sine (220.0, 0.3f, 4800);
            const auto playing = rig.play (tone, 4800);
            expect (rms (playing, 0, 2400) > 0.05f, "playing again on the same cue");
        }

        beginTest ("Latched panic waits for a second press");
        {
            Rig rig (passThrough());
            rig.engine.setPanicMode (AudioEngine::PanicMode::latch);
            const auto tone = sine (110.0, 0.3f, 4800);
            rig.play (tone, 4800);

            rig.engine.panic();
            expect (peak (rig.play (tone, 96000), 480, 96000) < 1.0e-6f, "still silent 2 s later");

            rig.engine.panic();
            const auto back = rig.play (tone, 48000);
            expect (rms (back, 38400, 48000) > 0.1f, "back after the second press");
            expect (rig.engine.getPanicState() == AudioEngine::PanicState::none);

            // The return is a fade, not a jump.
            float biggestStep = 0.0f;

            for (size_t i = 1; i < back.size(); ++i)
                biggestStep = juce::jmax (biggestStep, std::abs (back[i] - back[i - 1]));

            expect (biggestStep < 0.3f * 2.0f * juce::MathConstants<float>::pi * 110.0f / (float) sampleRate * 1.05f,
                    "no click when coming back");
        }

        beginTest ("Tuner and panic footswitches");
        {
            Rig rig (passThrough());
            rig.engine.getMidiRouter().setBindings ({ { MidiAction::tuner, 1, { MidiTrigger::Kind::controlChange, 0, 20 } },
                                                      { MidiAction::panic, 1, { MidiTrigger::Kind::controlChange, 0, 21 } } });
            rig.play ({}, blockSize);

            auto press = [&] (int cc)
            {
                rig.engine.getMidiRouter().receive (0xb0, (juce::uint8) cc, 127);
                rig.play ({}, blockSize);
                rig.engine.getMidiRouter().receive (0xb0, (juce::uint8) cc, 0);
                rig.play ({}, blockSize);
            };

            press (20);
            expect (rig.engine.isTunerOpen(), "tuner opened");
            press (20);
            expect (! rig.engine.isTunerOpen(), "and closed");

            press (21);
            rig.play ({}, 960);
            expect (rig.engine.getPanicState() == AudioEngine::PanicState::silent, "panic from the floor");
        }

        beginTest ("Tuner and panic don't allocate on the audio thread");
        {
            Rig rig (passThrough());
            const auto tone = sine (110.0, 0.3f, 4800);
            const auto before = realtimeAllocationCount();

            rig.engine.setTunerOpen (true);
            rig.play (tone, 9600);
            rig.engine.setTunerOpen (false);
            rig.engine.panic();
            rig.play (tone, 48000);

            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static StageTests stageTests;

} // namespace encore::test
