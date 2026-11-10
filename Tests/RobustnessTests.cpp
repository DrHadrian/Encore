// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "FakeDevice.h"
#include "TestSignals.h"

#include "engine/AudioEngine.h"
#include "engine/blocks/DelayBlock.h"
#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/nam/NamModel.h"

namespace encore::test
{

/** A block that turns its output into NaN when told to (an unstable filter,
    a broken plugin). Resetting it clears the fault, like clearing state would. */
class ExplodingBlock final : public Block
{
public:
    ExplodingBlock() : Block ("testExplode", "Explode") {}

    std::atomic<bool> explode { false };
    std::atomic<int> resets { 0 };

private:
    void prepareBlock (const ProcessSpec&) override {}
    void resetBlock() noexcept override { explode = false; ++resets; }
    void processBlock (float* const* ch, int numChannels, int n, const ProcessContext&) noexcept override
    {
        if (explode)
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < n; ++i)
                    ch[c][i] = std::numeric_limits<float>::quiet_NaN();
    }
};

class RobustnessTests final : public juce::UnitTest
{
public:
    RobustnessTests() : juce::UnitTest ("Robustness", "Encore") {}

    struct Rig
    {
        FakeDevice device;
        AudioEngine engine;
        std::vector<float> outL = std::vector<float> ((size_t) blockSize), outR = outL;

        Rig()
        {
            engine.audioDeviceAboutToStart (&device);
            engine.setMasterGainDecibels (0.0f);
            engine.setMuted (false);
        }

        ~Rig() { engine.audioDeviceStopped(); }

        /** One callback; returns the peak (or NaN/inf if something got through). */
        float callback (const float* input)
        {
            const float* inputs[] = { input };
            float* outputs[] = { outL.data(), outR.data() };
            engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, {});

            float sum = 0.0f;

            for (auto x : outL)
                sum = std::isfinite (x) ? juce::jmax (sum, std::abs (x)) : x;

            return sum;
        }
    };

    static std::unique_ptr<NamModel> model (const char* name)
    {
        juce::String error;
        return NamModel::load (juce::File (ENCORE_NAM_EXAMPLE_MODELS).getChildFile (name), spec, error);
    }

    void runTest() override
    {
        beginTest ("A patch that produces NaN is silenced and reset, never heard");
        {
            Rig rig;
            auto chain = std::make_unique<Chain>();
            auto& bomb = dynamic_cast<ExplodingBlock&> (chain->addBlock (std::make_unique<ExplodingBlock>()));
            rig.engine.setChain (std::move (chain));
            const auto tone = sine (220.0, 0.3f, blockSize);

            for (int i = 0; i < 50; ++i)
                rig.callback (tone.data());

            bomb.explode = true;
            const auto during = rig.callback (tone.data());
            expect (std::isfinite (during), "nothing but finite samples reach the output");
            expectEquals (rig.engine.getPlayer().getBadOutputCount(), 1);
            expect (bomb.resets > 0, "the patch was reset");

            float after = 0.0f;

            for (int i = 0; i < 50; ++i)
                after = rig.callback (tone.data());

            expect (after > 0.1f, "and it plays again: " + juce::String (after));
        }

        beginTest ("Garbage input from a driver is silenced");
        {
            Rig rig;
            auto chain = std::make_unique<Chain>();
            chain->addBlock (std::make_unique<EqBlock>());
            rig.engine.setChain (std::move (chain));

            std::vector<float> garbage ((size_t) blockSize, 0.1f);
            garbage[17] = std::numeric_limits<float>::infinity();
            garbage[40] = std::numeric_limits<float>::quiet_NaN();

            expect (std::isfinite (rig.callback (garbage.data())));
            expect (rig.engine.getPlayer().getBadInputCount() >= 1);

            const auto tone = sine (220.0, 0.3f, blockSize);
            float after = 0.0f;

            for (int i = 0; i < 50; ++i)
                after = rig.callback (tone.data());

            expect (after > 0.1f, "the EQ's filters weren't poisoned");
        }

        beginTest ("The engine reports a heartbeat");
        {
            Rig rig;
            const auto before = rig.engine.getCallbackCount();
            const auto silence = std::vector<float> ((size_t) blockSize, 0.0f);
            rig.callback (silence.data());
            rig.callback (silence.data());
            expectEquals ((int) (rig.engine.getCallbackCount() - before), 2);
        }

        soakTest();
    }

    /** Random cue switching, stomps, pedals, panic, tuner, edits and glitches
        while "playing", checking every buffer. ENCORE_SOAK_SECONDS sets how
        much audio to run (default 120 s of audio, which runs much faster than
        real time). Run with e.g. 14400 for a 4-hour soak before a tour. */
    void soakTest()
    {
        const auto seconds = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("ENCORE_SOAK_SECONDS", "120").getIntValue());
        beginTest ("Soak: " + juce::String (seconds) + " s of audio with random cues, stomps, pedals, panic and edits");

        Rig rig;
        auto& player = rig.engine.getPlayer();
        auto& router = rig.engine.getMidiRouter();
        juce::Random random (42);

        router.setBindings ({ { MidiAction::nextCue,     1, { MidiTrigger::Kind::controlChange, 0, 64 } },
                              { MidiAction::previousCue, 1, { MidiTrigger::Kind::controlChange, 0, 65 } },
                              { MidiAction::stomp,       1, { MidiTrigger::Kind::controlChange, 0, 80 } },
                              { MidiAction::expression,  1, { MidiTrigger::Kind::controlChange, 0, 11 } },
                              { MidiAction::panic,       1, { MidiTrigger::Kind::controlChange, 0, 90 } },
                              { MidiAction::tuner,       1, { MidiTrigger::Kind::controlChange, 0, 91 } } });

        // Four patches: two NAM amps with delays (tails), a gated one, and one
        // that rings out whole.
        auto makeChain = [&] (int kind)
        {
            auto chain = std::make_unique<Chain>();

            if (kind == 0 || kind == 3)
            {
                auto amp = std::make_unique<NamAmpBlock>();
                amp->installBeforePlaying (model (kind == 0 ? "wavenet_a1_standard.nam" : "lstm.nam"));
                chain->addBlock (std::move (amp));
            }

            if (kind == 2)
                chain->addBlock (std::make_unique<NoiseGateBlock>());

            auto& eq = chain->addBlock (std::make_unique<EqBlock>());
            chain->addExpressionTarget (1, *eq.findParameter ("treble"), -12.0f, 12.0f);

            if (kind != 2)
            {
                auto& delay = chain->addBlock (std::make_unique<DelayBlock>());
                delay.setStomp (1);
                delay.findParameter ("feedback")->setValue (70.0f);
            }

            chain->setTailMode (kind == 1 ? Chain::TailMode::fullChain : Chain::TailMode::tailsOnly);
            return chain;
        };

        std::vector<Chain*> chains;

        for (int k = 0; k < 4; ++k)
            chains.push_back (player.adopt (makeChain (k)));

        auto publish = [&]
        {
            std::vector<PatchPlayer::Cue> cues;

            for (int i = 0; i < 12; ++i)
                cues.push_back ({ chains[(size_t) (i % 4)], i % 5 == 0, i });

            player.publish (cues);
        };

        publish();
        player.goToCue (0);

        // A "guitar": notes with silences between, so tails get to ring out.
        std::vector<float> input ((size_t) blockSize);
        double phase = 0.0, frequency = 110.0;
        int noteSamples = 0;

        const auto totalBlocks = (int) (seconds * sampleRate / blockSize);
        const auto allocationsBefore = realtimeAllocationCount();
        int badBuffers = 0, overCeiling = 0, maxRinging = 0, edits = 0;
        float loudest = 0.0f;

        for (int block = 0; block < totalBlocks; ++block)
        {
            for (auto& x : input)
            {
                if (--noteSamples <= 0)
                {
                    noteSamples = random.nextInt ({ 2000, 40000 });
                    frequency = 82.4 * std::pow (2.0, random.nextInt (24) / 12.0);
                }

                const auto envelope = noteSamples > 1000 ? 0.4f : 0.0f;
                phase += frequency / sampleRate;
                x = envelope * (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
            }

            // A glitching driver now and then.
            if (random.nextInt (5000) == 0)
                input[(size_t) random.nextInt (blockSize)] = std::numeric_limits<float>::quiet_NaN();

            // The player, on stage.
            const auto r = random.nextInt (400);

            if (r < 4)        { router.receive (0xb0, 64, 127); router.receive (0xb0, 64, 0); }
            else if (r < 5)   { router.receive (0xb0, 65, 127); router.receive (0xb0, 65, 0); }
            else if (r < 6)   { router.receive (0xb0, 80, 127); router.receive (0xb0, 80, 0); }
            else if (r < 14)  router.receive (0xb0, 11, (juce::uint8) random.nextInt (128));
            else if (r == 14 && random.nextInt (20) == 0) { router.receive (0xb0, 90, 127); router.receive (0xb0, 90, 0); }
            else if (r == 15 && random.nextInt (10) == 0) { router.receive (0xb0, 91, 127); router.receive (0xb0, 91, 0); }
            else if (r == 16) player.goToCue (random.nextInt (12));

            const auto peak = rig.callback (input.data());

            if (! std::isfinite (peak))
                ++badBuffers;

            if (peak > 1.0f)
                ++overCeiling;

            loudest = juce::jmax (loudest, std::isfinite (peak) ? peak : 0.0f);
            maxRinging = juce::jmax (maxRinging, player.getNumRinging());

            // The UI timer, ~30 times a second: free garbage; sometimes an edit
            // rebuilds a patch (new chain in, old one released).
            if (block % 12 == 0)
            {
                rig.engine.collectGarbage();

                if (random.nextInt (200) == 0)
                {
                    const auto which = (size_t) random.nextInt (4);
                    auto* old = chains[which];
                    chains[which] = player.adopt (makeChain ((int) which));
                    player.release (old);   // before publishing: the next table leaves it out
                    publish();

                    if (player.getActiveChain() == old)   // as the session does: keep playing the new build
                        player.play (chains[which]);
                    ++edits;
                }
            }
        }

        // Let everything ring out and be freed.
        std::fill (input.begin(), input.end(), 0.0f);

        for (int block = 0; block < (int) (12.0 * sampleRate / blockSize); ++block)
        {
            rig.callback (input.data());

            if (block % 12 == 0)
                rig.engine.collectGarbage();
        }

        rig.engine.collectGarbage();

        expectEquals (badBuffers, 0, "no NaN/inf ever reached the output");
        expectEquals (overCeiling, 0, "never above full scale");
        expectEquals (realtimeAllocationCount() - allocationsBefore, 0, "no allocations on the audio thread");
        expect (maxRinging <= PatchPlayer::maxFading, "ringing patches bounded");
        expectEquals (player.getDroppedCommands(), 0, "no commands lost");
        expectEquals (player.getNumOwnedChains(), 4, "replaced patches were all freed");
        expect (loudest > 0.05f, "it actually played");
        logMessage ("  soak: " + juce::String (edits) + " patch rebuilds, " + juce::String (player.getBadInputCount())
                    + " glitches absorbed, loudest " + juce::String (loudest, 2));
    }
};

static RobustnessTests robustnessTests;

} // namespace encore::test
