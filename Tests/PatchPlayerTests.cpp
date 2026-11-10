// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/DelayBlock.h"
#include "engine/core/PatchPlayer.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

/** A block that only changes level: makes patches easy to tell apart. */
class GainTestBlock final : public Block
{
public:
    GainTestBlock() : Block ("testGain", "Gain"), gain (addParameter ({ "gain", "Gain", { -60.0f, 12.0f }, 0.0f, "dB", 1 })) {}

private:
    void prepareBlock (const ProcessSpec&) override {}
    void resetBlock() noexcept override {}
    void processBlock (float* const* ch, int numChannels, int n, const ProcessContext&) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto g = juce::Decibels::decibelsToGain (gain.nextValue());

            for (int c = 0; c < numChannels; ++c)
                ch[c][i] *= g;
        }
    }

    Parameter& gain;
};

class PatchPlayerTests final : public juce::UnitTest
{
public:
    PatchPlayerTests() : juce::UnitTest ("Patch switching", "Encore") {}

    static std::unique_ptr<Chain> gainChain (float dB, const char* label)
    {
        auto c = std::make_unique<Chain>();
        c->addBlock (std::make_unique<GainTestBlock>()).findParameter ("gain")->setValue (dB);
        c->label = label;
        return c;
    }

    static std::unique_ptr<Chain> delayChain (const char* label, Chain::TailMode mode)
    {
        auto c = gainChain (0.0f, label);
        auto& delay = c->addBlock (std::make_unique<DelayBlock>());
        delay.findParameter ("time")->setValue (100.0f);
        delay.findParameter ("feedback")->setValue (50.0f);
        delay.findParameter ("mix")->setValue (100.0f);
        c->setTailMode (mode);
        return c;
    }

    /** Runs a signal through the player in blocks; calls between(blockIndex) before each block. */
    template <typename Between>
    static std::vector<float> run (PatchPlayer& player, std::vector<float> signal, Between&& between)
    {
        for (int offset = 0, b = 0; offset < (int) signal.size(); offset += blockSize, ++b)
        {
            between (b);
            player.process (signal.data() + offset, juce::jmin (blockSize, (int) signal.size() - offset));
        }

        return signal;
    }

    static std::vector<float> run (PatchPlayer& player, std::vector<float> signal)
    {
        return run (player, std::move (signal), [] (int) {});
    }

    void runTest() override
    {
        beginTest ("Switching between identical patches is mathematically seamless");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* a = player.adopt (gainChain (0.0f, "A"));
            auto* b = player.adopt (gainChain (0.0f, "B"));

            // Equal fade-out and fade-in times: the two inputs always sum to one.
            a->setSwitchFades (0.01, 0.01);
            b->setSwitchFades (0.01, 0.01);
            player.publish ({ { a }, { b } });
            player.goToCue (0);

            const auto input = sine (220.0, 0.5f, 9600);
            const auto output = run (player, input, [&] (int block) { if (block == 30) player.next(); });

            // (The very first patch fades in from silence over 10 ms, so a show never
            // starts with a pop; compare from after that.)
            float maxError = 0.0f;
            size_t worstAt = 0;
            for (size_t i = 960; i < input.size(); ++i)
                if (std::abs (output[i] - input[i]) > maxError)
                {
                    maxError = std::abs (output[i] - input[i]);
                    worstAt = i;
                }

            expectLessThan (maxError, 1.0e-5f, "no gap, no click: the two inputs always sum to one (worst at sample "
                                                 + juce::String ((int) worstAt) + ")");
            expect (player.getActiveChain() == b);
            expectEquals (player.getCurrentCue(), 1);
        }

        beginTest ("The old patch rings out for its ring-out time; the new one fades in over its fade-in time");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* loud = player.adopt (gainChain (0.0f, "Loud"));
            auto* quiet = player.adopt (gainChain (-80.0f, "Almost silent"));
            loud->setSwitchFades (0.02, 0.15);
            quiet->setSwitchFades (0.02, 0.15);
            player.publish ({ { loud }, { quiet }, { loud } });
            player.goToCue (0);

            const auto blocksPerMs = sampleRate / 1000.0 / blockSize;
            const auto switchBlock = 40;
            const auto at = [&] (double msAfterSwitch) { return (int) ((switchBlock + msAfterSwitch * blocksPerMs) * blockSize); };

            // Loud -> quiet: what we hear is the loud patch ringing out.
            const auto input = sine (440.0, 0.5f, at (400.0));
            const auto output = run (player, input, [&] (int block) { if (block == switchBlock) player.next(); });
            const auto inputRms = rms (input, at (70.0), at (80.0));
            expectWithinAbsoluteError (rms (output, at (70.0), at (80.0)) / inputRms, 0.5f, 0.06f, "half-way through a 150 ms ring-out");
            expectLessThan (rms (output, at (160.0), at (200.0)) / inputRms, 0.01f, "gone after 150 ms");

            // Quiet -> loud: comes in over 20 ms.
            const auto output2 = run (player, input, [&] (int block) { if (block == switchBlock) player.next(); });
            expectWithinAbsoluteError (rms (output2, at (9.0), at (11.0)) / inputRms, 0.5f, 0.08f, "half-way through a 20 ms fade-in");
            expectWithinAbsoluteError (rms (output2, at (30.0), at (60.0)) / inputRms, 1.0f, 0.02f, "fully in after 20 ms");
        }

        beginTest ("Switching to a quieter patch glides, with no jump");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* a = player.adopt (gainChain (0.0f, "A"));
            auto* b = player.adopt (gainChain (-12.0f, "B"));
            player.publish ({ { a }, { b } });
            player.goToCue (0);

            const auto output = run (player, sine (110.0, 0.5f, 9600), [&] (int block) { if (block == 30) player.next(); });

            // A smooth 110 Hz sine changes by at most 2*pi*110/48000 x its peak per
            // sample. (The patches overlap briefly, so the peak can rise a little;
            // a click would jump far more than this.)
            float maxStep = 0.0f;
            for (size_t i = 1; i < output.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (output[i] - output[i - 1]));

            const auto smoothLimit = (float) (2.0 * juce::MathConstants<double>::pi * 110.0 / sampleRate)
                                     * peak (output, 0, (int) output.size()) * 1.05f;
            expectLessThan (maxStep, smoothLimit);
        }

        beginTest ("Delay repeats ring out after switching away (tails only)");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* withDelay = player.adopt (delayChain ("Delay", Chain::TailMode::tailsOnly));
            auto* dry = player.adopt (gainChain (0.0f, "Dry"));
            player.publish ({ { withDelay }, { dry } });
            player.goToCue (0);

            // A short burst into the delay patch, then switch and play silence.
            auto signal = sine (440.0, 0.5f, 4800);
            signal.resize (48000, 0.0f);
            const auto output = run (player, signal, [&] (int block) { if (block == 40) player.next(); });

            // Echoes at 100 ms steps after the burst, long after the switch at ~107 ms.
            expectGreaterThan (rms (output, 14400, 19200), 0.01f, "repeats still sound after the switch");
            expect (player.getActiveChain() == dry);

            // Feedback 50 %: below -90 dB after about 1.5 s, then the ringing patch stops.
            run (player, std::vector<float> (48000 * 3, 0.0f));
            expectEquals (player.getNumRinging(), 0, "it stops once silent");
        }

        beginTest ("A patch with nothing that rings stops right after settling");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* a = player.adopt (gainChain (0.0f, "A"));
            auto* b = player.adopt (gainChain (0.0f, "B"));
            player.publish ({ { a }, { b } });
            player.goToCue (0);
            run (player, sine (220.0, 0.5f, 9600));
            player.next();
            run (player, std::vector<float> ((size_t) (0.1 * sampleRate), 0.0f));
            expectEquals (player.getNumRinging(), 1, "still settling");
            run (player, std::vector<float> ((size_t) (0.3 * sampleRate), 0.0f));
            expectEquals (player.getNumRinging(), 0);
        }

        beginTest ("Same patch on the next cue: keep, or reset to saved settings");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* a = player.adopt (gainChain (-6.0f, "A"));
            a->saveSnapshot();
            player.publish ({ { a, false }, { a, false }, { a, true } });
            player.goToCue (0);
            run (player, std::vector<float> (blockSize, 0.0f));

            auto* gain = a->getBlocks()[0]->findParameter ("gain");
            gain->setValue (3.0f);   // a live tweak
            player.next();
            run (player, std::vector<float> (blockSize, 0.0f));
            expectEquals (gain->getValue(), 3.0f, "keep: the tweak stays");

            player.next();
            run (player, std::vector<float> (blockSize, 0.0f));
            expectEquals (gain->getValue(), -6.0f, "reset: back to the saved value");
            expectEquals (player.getNumRinging(), 0, "nothing was switched");
        }

        beginTest ("Next and previous stop at the ends");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* a = player.adopt (gainChain (0.0f, "A"));
            auto* b = player.adopt (gainChain (0.0f, "B"));
            player.publish ({ { a }, { b } });

            player.previous();   // from "nowhere": goes to the first cue
            run (player, std::vector<float> (blockSize, 0.0f));
            expectEquals (player.getCurrentCue(), 0);

            for (int i = 0; i < 5; ++i)
                player.next();

            run (player, std::vector<float> (blockSize, 0.0f));
            expectEquals (player.getCurrentCue(), 1);
            expect (player.getActiveChain() == b);
        }

        beginTest ("Going back to a patch that is still ringing picks it up, tail and all");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* withDelay = player.adopt (delayChain ("Delay", Chain::TailMode::tailsOnly));
            auto* dry = player.adopt (gainChain (0.0f, "Dry"));
            player.publish ({ { withDelay }, { dry } });
            player.goToCue (0);
            run (player, sine (440.0, 0.5f, 4800));
            player.next();
            run (player, std::vector<float> (4800, 0.0f));
            player.previous();
            const auto output = run (player, std::vector<float> (4800, 0.0f));

            expect (player.getActiveChain() == withDelay);
            expectGreaterThan (rms (output, 0, 4800), 0.005f, "its repeats are still there");
        }

        beginTest ("A removed patch is freed only once nothing can reach it");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto* withDelay = player.adopt (delayChain ("Delay", Chain::TailMode::tailsOnly));
            auto* other = player.adopt (gainChain (0.0f, "Other"));
            player.publish ({ { withDelay }, { other } });
            player.goToCue (0);
            run (player, sine (440.0, 0.5f, 4800));

            player.release (withDelay);
            player.publish ({ { other } });
            run (player, std::vector<float> (blockSize, 0.0f));
            player.collectGarbage();
            expectEquals (player.getNumOwnedChains(), 2, "still playing: kept");

            player.play (other);
            player.play (withDelay);   // refers to a removed patch: must be ignored safely
            run (player, std::vector<float> (blockSize, 0.0f));
            expect (player.getActiveChain() == other);
            player.collectGarbage();
            expectEquals (player.getNumOwnedChains(), 2, "still ringing out: kept");

            run (player, std::vector<float> (48000 * 4, 0.0f));
            player.collectGarbage();
            expectEquals (player.getNumOwnedChains(), 1, "freed once silent");
        }

        beginTest ("Hammering switches never allocates, never blows up");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            std::vector<PatchPlayer::Cue> cues;

            for (int i = 0; i < 6; ++i)
                cues.push_back ({ player.adopt (i % 2 == 0 ? delayChain ("D", i % 4 == 0 ? Chain::TailMode::fullChain
                                                                                          : Chain::TailMode::tailsOnly)
                                                           : gainChain ((float) -i, "G")),
                                  i == 3 });
            player.publish (cues);

            juce::Random random (7);
            auto signal = sine (330.0, 0.5f, blockSize);
            const auto before = realtimeAllocationCount();
            bool finite = true;

            for (int i = 0; i < 3000; ++i)
            {
                switch (random.nextInt (6))
                {
                    case 0: player.next(); break;
                    case 1: player.previous(); break;
                    case 2: player.goToCue (random.nextInt (6)); break;
                    default: break;
                }

                const rt::ScopedRealtimeSection realtime;
                player.process (signal.data(), blockSize);

                for (auto x : signal)
                    finite = finite && std::isfinite (x) && std::abs (x) < 10.0f;

                for (int s = 0; s < blockSize; ++s)   // keep feeding a tone, not the output
                    signal[(size_t) s] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (i * blockSize + s) / sampleRate);
            }

            expect (finite);
            expectEquals (realtimeAllocationCount() - before, 0);
            expectEquals (player.getDroppedCommands(), 0);
        }
    }
};

static PatchPlayerTests patchPlayerTests;

} // namespace encore::test
