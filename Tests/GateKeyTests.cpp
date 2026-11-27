// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2 step 4: key sources in the engine. Interface
// inputs and the tuner through ProcessContext, taps (a block keyed from an
// earlier block's output), what "earlier" means in a split, and falling back
// to the gate's own input when a reference doesn't resolve.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/GateBlock.h"
#include "engine/blocks/SplitBlocks.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    /** Multiplies by a gain. */
    class Gain final : public Block
    {
    public:
        explicit Gain (float g, const char* id = "") : Block ("testGain", "Gain"), gain (g) { setModelId (id); }

    private:
        void prepareBlock (const ProcessSpec&) override {}
        void resetBlock() noexcept override {}
        void processBlock (float* const* ch, int n, int len, const ProcessContext&) noexcept override
        {
            for (int c = 0; c < n; ++c)
                juce::FloatVectorOperations::multiply (ch[c], gain, len);
        }

        float gain;
    };

    /** Puts out its key: the tap it was given, else silence. Records what
        the chain told it, and what the context carried. */
    class KeyProbe final : public Block
    {
    public:
        explicit KeyProbe (const char* id = "probe") : Block ("testKeyProbe", "Key probe") { setModelId (id); }

        void setKeyTap (const float* const* channels, int numChannels, const juce::String& why) override
        {
            tap = channels;
            tapChannels = numChannels;
            problem = why;
        }

        void setKeyInputSlot (int slot) noexcept override { inputSlot = slot; }

        const float* const* tap = nullptr;
        int tapChannels = 0;
        juce::String problem;
        int inputSlot = -1;
        bool sawTuner = false;
        int sawInputs = 0;

    private:
        void prepareBlock (const ProcessSpec&) override {}
        void resetBlock() noexcept override {}
        void processBlock (float* const* ch, int n, int len, const ProcessContext& context) noexcept override
        {
            sawTuner = context.tunerOpen;
            sawInputs = context.numInputs;

            for (int c = 0; c < n; ++c)
                for (int i = 0; i < len; ++i)
                    ch[c][i] = tap != nullptr ? tap[juce::jmin (c, tapChannels - 1)][i] : 0.0f;
        }
    };

    /** A gate whose setup a test sets directly. */
    class TestGate final : public GateBlock
    {
    public:
        explicit TestGate (const char* id = "gate") : GateBlock ("testGate", "Gate") { setModelId (id); }
        GateSetup setup;

    private:
        GateSetup readSetup (int) noexcept override { return setup; }
    };

    std::unique_ptr<Block> keyed (std::unique_ptr<Block> block, const juce::String& keyBlock)
    {
        block->setRefs ({ { "keyBlock", keyBlock } });
        return block;
    }

    std::vector<float> runMono (Chain& chain, std::vector<float> signal, const ChainInputs& inputs = {})
    {
        std::vector<float> right (signal.size());

        for (int offset = 0; offset < (int) signal.size(); offset += blockSize)
        {
            const auto len = juce::jmin (blockSize, (int) signal.size() - offset);
            float* ch[2] = { signal.data() + offset, right.data() + offset };
            chain.process (ch, len, inputs);
        }

        return signal;
    }
}

class GateKeyTests final : public juce::UnitTest
{
public:
    GateKeyTests() : juce::UnitTest ("Gate keys", "Encore") {}

    void runTest() override
    {
        beginTest ("The context carries the interface's inputs and the tuner to every block");
        {
            Chain chain;
            auto& probe = static_cast<KeyProbe&> (chain.addBlock (std::make_unique<KeyProbe>()));
            chain.prepare (spec);

            std::vector<float> other ((size_t) blockSize, 0.25f);
            const float* inputs[8] = { nullptr, other.data() };
            std::vector<float> io ((size_t) blockSize, 0.0f), right ((size_t) blockSize);
            float* ch[2] = { io.data(), right.data() };
            chain.process (ch, blockSize, { inputs, 8, true });
            expect (probe.sawTuner);
            expectEquals (probe.sawInputs, 8);

            chain.process (ch, blockSize);
            expect (! probe.sawTuner, "absent by default");
        }

        beginTest ("Keying from a block equals keying from a copy of its output");
        {
            Chain chain;
            chain.addBlock (std::make_unique<Gain> (0.5f, "a"));
            chain.addBlock (std::make_unique<Gain> (0.0f, "mute"));   // what's in between doesn't matter
            auto& probe = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe>(), "a")));
            chain.prepare (spec);
            expect (chain.isTap (0) && ! chain.isTap (1));
            expect (probe.tap != nullptr && probe.problem.isEmpty());

            const auto in = sine (440.0, 0.8f, blockSize * 4);
            const auto out = runMono (chain, in);
            auto maxError = 0.0f;

            for (size_t i = 0; i < in.size(); ++i)
                maxError = juce::jmax (maxError, std::abs (out[i] - in[i] * 0.5f));

            expectEquals (maxError, 0.0f);
        }

        beginTest ("What \"earlier\" means, path by path");
        {
            // 0 pre | 1 split | 2 A | 3 merge | 4 post || 5 B, 6 B
            Chain chain;
            chain.addBlock (std::make_unique<Gain> (1.0f, "pre"));
            chain.addBlock (std::make_unique<SplitBlock>());
            chain.addBlock (std::make_unique<Gain> (1.0f, "a"));
            chain.addBlock (std::make_unique<MergeBlock>());
            chain.addBlock (std::make_unique<Gain> (1.0f, "post"));
            chain.addBlock (std::make_unique<Gain> (1.0f, "b1"), true);
            chain.addBlock (std::make_unique<Gain> (1.0f, "b2"), true);
            chain.prepare (spec);
            expect (chain.hasSplit());

            expect (chain.isEarlier (0, 2) && chain.isEarlier (0, 5) && chain.isEarlier (0, 4), "the main lane before the split: before everything after it");
            expect (chain.isEarlier (5, 6) && ! chain.isEarlier (6, 5), "path B: its own earlier blocks");
            expect (! chain.isEarlier (2, 5) && ! chain.isEarlier (2, 6), "path B doesn't count path A as before it");
            expect (! chain.isEarlier (5, 2), "nor path A path B");
            expect (chain.isEarlier (2, 4) && chain.isEarlier (6, 4), "after the merge: both paths are before it");
            expect (! chain.isEarlier (4, 0) && ! chain.isEarlier (4, 2) && ! chain.isEarlier (2, 2), "nothing later, nothing itself");
            expectEquals (chain.indexOfModelId ("b2"), 6);
        }

        beginTest ("A key that doesn't resolve: refused at build, with the reason");
        {
            Chain chain;
            chain.addBlock (std::make_unique<Gain> (1.0f, "pre"));
            chain.addBlock (std::make_unique<SplitBlock>());
            chain.addBlock (std::make_unique<Gain> (1.0f, "a"));
            chain.addBlock (std::make_unique<MergeBlock>());
            auto& later = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe> ("p1"), "post")));
            chain.addBlock (std::make_unique<Gain> (1.0f, "post"));
            auto& missing = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe> ("p2"), "nowhere")));
            auto& acrossPaths = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe> ("p3"), "a"), true));
            auto& fine = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe> ("p4"), "pre"), true));
            chain.prepare (spec);

            expect (later.tap == nullptr && later.problem == "Key: that block isn't before this gate");
            expect (missing.tap == nullptr && missing.problem == "Key: that block isn't in this patch");
            expect (acrossPaths.tap == nullptr && acrossPaths.problem == "Key: that block isn't before this gate");
            expect (fine.tap != nullptr && fine.problem.isEmpty());
        }

        beginTest ("A gate whose key fell back listens to its own input, and says why");
        {
            // A ducker keyed from a block that isn't there: on its own input it
            // ducks its own loud signal; keyed from silence it wouldn't.
            Chain chain;
            auto& gate = static_cast<TestGate&> (chain.addBlock (keyed (std::make_unique<TestGate>(), "gone")));
            gate.setup.way = GateWay::ducker;
            gate.setup.key = GateKey::block;
            gate.setup.thresholdDb = -40.0f;
            gate.setup.rangeDb = -12.0f;
            gate.setup.attackMs = 1.0f;
            chain.prepare (spec);
            chain.reset();

            const auto out = runMono (chain, std::vector<float> (4800, 0.5f));
            expectWithinAbsoluteError (out.back(), 0.5f * juce::Decibels::decibelsToGain (-12.0f), 1.0e-4f, "ducked by its own input");
            expect (gate.getKeyState() == GateBlock::KeyState::blockNotUsable);
            expectEquals (gate.getKeyInfo(), juce::String ("Key: that block isn't in this patch"));

            TestGate unpicked;
            unpicked.setup.key = GateKey::block;
            unpicked.prepare (spec);
            std::vector<float> buffer ((size_t) blockSize, 0.1f);
            float* ch[1] = { buffer.data() };
            unpicked.process (ch, 1, blockSize);
            expect (unpicked.getKeyState() == GateBlock::KeyState::noBlockPicked);
            expect (unpicked.getKeyInfo().contains ("no block picked"));
        }

        beginTest ("An audio input key: used when it's there, else its own input, and the info says so");
        {
            TestGate gate;
            gate.setRefs ({ { "keyInput", "Input 3" } });
            gate.setup.key = GateKey::audioInput;
            gate.setup.thresholdDb = -40.0f;
            gate.setup.holdMs = 0.0f;
            gate.setup.releaseMs = 5.0f;
            gate.prepare (spec);
            gate.reset();

            // Its own input is loud; Input 3 is silent: keyed from Input 3 it closes.
            std::vector<float> silent ((size_t) blockSize, 0.0f);
            const float* inputs[8] = { nullptr, nullptr, silent.data() };
            gate.setKeyInputSlot (2);
            std::vector<float> buffer ((size_t) blockSize);
            ProcessContext context;
            context.inputs = inputs;
            context.numInputs = 8;

            for (int k = 0; k < 40; ++k)
            {
                std::fill (buffer.begin(), buffer.end(), 0.5f);
                float* ch[1] = { buffer.data() };
                gate.process (ch, 1, blockSize, context);
            }

            expect (buffer.back() < 1.0e-3f, "keyed from the silent input: closed");
            expect (gate.getKeyState() == GateBlock::KeyState::inUse);

            gate.setKeyInputSlot (-1);   // the routing changed: Input 3 is gone

            for (int k = 0; k < 40; ++k)
            {
                std::fill (buffer.begin(), buffer.end(), 0.5f);
                float* ch[1] = { buffer.data() };
                gate.process (ch, 1, blockSize, context);
            }

            expectWithinAbsoluteError (buffer.back(), 0.5f, 1.0e-6f, "its own input: open");
            expect (gate.getKeyState() == GateBlock::KeyState::inputNotHere);
            expectEquals (gate.getKeyInfo(), juce::String ("Key: Input 3 isn't on this computer"));
        }

        beginTest ("Tails only: a skipped block's tap is silence");
        {
            Chain chain;
            chain.addBlock (std::make_unique<Gain> (1.0f, "a"));
            chain.addBlock (std::make_unique<Gain> (1.0f, "b"));
            auto& probe = static_cast<KeyProbe&> (chain.addBlock (keyed (std::make_unique<KeyProbe>(), "a")));
            chain.prepare (spec);
            runMono (chain, std::vector<float> ((size_t) blockSize, 0.7f));
            expectEquals (probe.tap[0][blockSize - 1], 0.7f);

            std::vector<float> io ((size_t) blockSize, 0.0f), right ((size_t) blockSize);
            float* ch[2] = { io.data(), right.data() };
            chain.processFrom (1, ch, blockSize);
            expectEquals (probe.tap[0][blockSize - 1], 0.0f, "what the skipped block would have passed on");
        }

        beginTest ("No allocation on the audio thread with taps and inputs in use");
        {
            Chain chain;
            chain.addBlock (std::make_unique<Gain> (0.5f, "a"));
            auto& gate = static_cast<TestGate&> (chain.addBlock (keyed (std::make_unique<TestGate>(), "a")));
            gate.setup.key = GateKey::block;
            auto& other = static_cast<TestGate&> (chain.addBlock (std::make_unique<TestGate> ("g2")));
            other.setRefs ({ { "keyInput", "Mic" } });
            other.setup.key = GateKey::audioInput;
            other.setKeyInputSlot (1);
            chain.prepare (spec);

            std::vector<float> mic ((size_t) blockSize, 0.1f), io ((size_t) blockSize), right ((size_t) blockSize);
            const float* inputs[8] = { nullptr, mic.data() };
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int k = 0; k < 200; ++k)
                {
                    std::fill (io.begin(), io.end(), k % 2 == 0 ? 0.3f : 0.0f);
                    float* ch[2] = { io.data(), right.data() };
                    chain.process (ch, blockSize, { inputs, 8, k % 3 == 0 });

                    if (k == 100)
                        other.setKeyInputSlot (-1);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static GateKeyTests gateKeyTests;

} // namespace encore::test
