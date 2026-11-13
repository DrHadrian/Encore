// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/DelayBlock.h"
#include "engine/blocks/SplitBlocks.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    /** Multiplies by a gain and delays by its latency (as a plugin would). */
    class TestGainBlock final : public Block
    {
    public:
        TestGainBlock (float g, int latencySamples = 0)
            : Block ("testGain", "Gain"), gain (g), latency (latencySamples) {}

        int getLatencySamples() const noexcept override { return latency; }
        int calls = 0;

    private:
        void prepareBlock (const ProcessSpec&) override
        {
            for (auto& line : lines)
                line.assign ((size_t) latency + 1, 0.0f);
        }

        void resetBlock() noexcept override
        {
            for (auto& line : lines)
                std::fill (line.begin(), line.end(), 0.0f);
        }

        void processBlock (float* const* ch, int numChannels, int n, const ProcessContext&) noexcept override
        {
            ++calls;

            for (int c = 0; c < numChannels; ++c)
            {
                auto& line = lines[(size_t) c];

                for (int i = 0; i < n; ++i)
                {
                    const auto x = ch[c][i] * gain;

                    if (latency == 0)
                    {
                        ch[c][i] = x;
                        continue;
                    }

                    // A line of latency + 1: shift by one each sample.
                    std::move (line.begin() + 1, line.end(), line.begin());
                    line.back() = x;
                    ch[c][i] = line.front();
                }
            }
        }

        float gain;
        int latency;
        std::array<std::vector<float>, 2> lines;
    };

    /** A mono-only block that records how many channels it was given. */
    class MonoProbe final : public Block
    {
    public:
        MonoProbe() : Block ("testMono", "Mono") {}
        int getOutputChannels (int) const noexcept override { return 1; }
        int lastChannels = 0;

    private:
        void prepareBlock (const ProcessSpec&) override {}
        void resetBlock() noexcept override {}
        void processBlock (float* const*, int numChannels, int, const ProcessContext&) noexcept override { lastChannels = numChannels; }
    };

    struct Stereo
    {
        std::vector<float> left = std::vector<float> ((size_t) blockSize), right = std::vector<float> ((size_t) blockSize);
        float* channels[2] = { left.data(), right.data() };

        int run (Chain& chain, float value)
        {
            std::fill (left.begin(), left.end(), value);
            std::fill (right.begin(), right.end(), 0.0f);
            return chain.process (channels, blockSize);
        }
    };
}

class SplitPathTests final : public juce::UnitTest
{
public:
    SplitPathTests() : juce::UnitTest ("Split paths", "Encore") {}

    /** gain 0.5 | split | A... | merge | post..., path B... */
    static std::unique_ptr<Chain> makeSplit (std::vector<std::unique_ptr<Block>> a, std::vector<std::unique_ptr<Block>> b,
                                             std::vector<std::unique_ptr<Block>> post = {})
    {
        auto chain = std::make_unique<Chain>();
        chain->addBlock (std::make_unique<TestGainBlock> (0.5f));
        chain->addBlock (std::make_unique<SplitBlock>());

        for (auto& block : a)
            chain->addBlock (std::move (block));

        chain->addBlock (std::make_unique<MergeBlock>());

        for (auto& block : post)
            chain->addBlock (std::move (block));

        for (auto& block : b)
            chain->addBlock (std::move (block), true);

        chain->prepare (spec);
        return chain;
    }

    template <typename... Blocks>
    static std::vector<std::unique_ptr<Block>> list (std::unique_ptr<Blocks>... blocks)
    {
        std::vector<std::unique_ptr<Block>> v;
        (v.push_back (std::move (blocks)), ...);
        return v;
    }

    static void settle (Chain& chain, Stereo& io, float value, int blocks = 40)
    {
        for (int i = 0; i < blocks; ++i)
            io.run (chain, value);
    }

    void runTest() override
    {
        beginTest ("Structure: indices, main lane, path B, stereo out");
        {
            auto chain = makeSplit (list (std::make_unique<TestGainBlock> (1.0f)),
                                    list (std::make_unique<TestGainBlock> (1.0f), std::make_unique<TestGainBlock> (1.0f)));
            expect (chain->hasSplit());
            expectEquals (chain->getSplitIndex(), 1);
            expectEquals (chain->getMergeIndex(), 3);
            expectEquals (chain->getNumMainBlocks(), 4);
            expectEquals ((int) chain->getBlocks().size(), 6);
            expectEquals (chain->getOutputChannels(), 2);
        }

        beginTest ("Both paths at 0 dB, centred: each on both sides");
        {
            auto chain = makeSplit (list (std::make_unique<TestGainBlock> (1.0f)), list (std::make_unique<TestGainBlock> (0.5f)));
            Stereo io;
            settle (*chain, io, 0.4f);
            expectEquals (io.run (*chain, 0.4f), 2);
            expectWithinAbsoluteError (io.left.back(), 0.2f + 0.1f, 1.0e-5f);
            expectWithinAbsoluteError (io.right.back(), 0.2f + 0.1f, 1.0e-5f);
        }

        beginTest ("Level and pan: A left, B right, B at -6 dB");
        {
            auto chain = makeSplit ({}, {});
            auto& merge = *chain->getBlocks()[2];
            merge.findParameter (mergeParams::panA)->setValue (-100.0f);
            merge.findParameter (mergeParams::panB)->setValue (100.0f);
            merge.findParameter (mergeParams::levelB)->setValue (-6.0f);
            Stereo io;
            settle (*chain, io, 0.4f);
            expectWithinAbsoluteError (io.left.back(), 0.2f, 1.0e-5f);
            expectWithinAbsoluteError (io.right.back(), 0.2f * std::pow (10.0f, -6.0f / 20.0f), 1.0e-5f);

            merge.findParameter (mergeParams::levelA)->setValue (MergeBlock::minLevelDb);
            settle (*chain, io, 0.4f);
            expectEquals (io.left.back(), 0.0f, "level at the bottom: off");
            expectEquals (MergeBlock::levelToGain (-1000.0f), 0.0f);
            expectEquals (merge.findParameter (mergeParams::levelA)->toText (-60.0f), juce::String ("Off"));
            expectEquals (merge.findParameter (mergeParams::panA)->toText (-30.0f), juce::String ("L 30"));
            expectEquals (merge.findParameter (mergeParams::panA)->toText (0.0f), juce::String ("C"));
        }

        beginTest ("The split's on/off is path B: it fades, then costs nothing");
        {
            auto b = std::make_unique<TestGainBlock> (1.0f);
            auto* probe = b.get();
            auto chain = makeSplit ({}, list (std::move (b)));
            auto& split = *chain->getBlocks()[1];
            Stereo io;
            settle (*chain, io, 0.4f);
            expectWithinAbsoluteError (io.left.back(), 0.4f, 1.0e-5f);

            split.setEnabled (false);
            io.run (*chain, 0.4f);
            expect (io.left[1] > 0.39f && io.left.back() < 0.4f && io.left.back() > 0.2f, "fades over 20 ms, not a jump");
            settle (*chain, io, 0.4f);
            expectWithinAbsoluteError (io.left.back(), 0.2f, 1.0e-5f, "path A only");

            const auto callsWhileOff = probe->calls;
            settle (*chain, io, 0.4f, 10);
            expectEquals (probe->calls, callsWhileOff, "path B sleeps while off");

            split.setEnabled (true);
            settle (*chain, io, 0.4f);
            expectWithinAbsoluteError (io.left.back(), 0.4f, 1.0e-5f, "and comes back");
        }

        beginTest ("Paths are lined up: a plugin's latency doesn't comb");
        {
            auto chain = makeSplit (list (std::make_unique<TestGainBlock> (1.0f, 64)), list (std::make_unique<TestGainBlock> (1.0f)));
            expectEquals (chain->getPathLatencyDifference(), -64);
            expect (! chain->isAlignmentLimited());
            expectEquals (chain->getLatencySamples(), 64);

            Stereo io;
            std::vector<float> heard;

            for (int block = 0; block < 3; ++block)
            {
                std::fill (io.left.begin(), io.left.end(), 0.0f);

                if (block == 0)
                    io.left[0] = 1.0f;

                chain->process (io.channels, blockSize);
                heard.insert (heard.end(), io.left.begin(), io.left.end());
            }

            int nonZero = 0;

            for (auto x : heard)
                if (std::abs (x) > 1.0e-6f)
                    ++nonZero;

            expectEquals (nonZero, 1, "one click, not two");
            expectWithinAbsoluteError (heard[64], 1.0f, 1.0e-5f, "both halves (0.5 + 0.5) at 64 samples");

            auto far = makeSplit (list (std::make_unique<TestGainBlock> (1.0f, Chain::maxAlignmentSamples + 10)), {});
            expect (far->isAlignmentLimited());
        }

        beginTest ("Channels: stereo after the merge; an amp after it gets the sum");
        {
            auto mono = std::make_unique<MonoProbe>();
            auto* probe = mono.get();
            auto chain = makeSplit ({}, {}, list (std::move (mono)));
            expectEquals (chain->getOutputChannels(), 1);

            auto& merge = *chain->getBlocks()[2];
            merge.findParameter (mergeParams::panA)->setValue (-100.0f);
            merge.findParameter (mergeParams::panB)->setValue (100.0f);
            merge.findParameter (mergeParams::levelB)->setValue (MergeBlock::minLevelDb);
            Stereo io;
            settle (*chain, io, 0.4f);
            expectEquals (probe->lastChannels, 1);
            expectWithinAbsoluteError (io.left.back(), 0.1f, 1.0e-5f, "(0.2 left + 0 right) / 2");
        }

        beginTest ("Tails inside a path ring out from the split");
        {
            auto chain = makeSplit (list (std::make_unique<DelayBlock>()), {});
            expectEquals (chain->getFirstTailBlock(), 1);

            auto plain = makeSplit ({}, list (std::make_unique<DelayBlock>()), list (std::make_unique<DelayBlock>()));
            expectEquals (plain->getFirstTailBlock(), 1, "path B's delay comes before the one after the merge");

            auto after = makeSplit ({}, {}, list (std::make_unique<DelayBlock>()));
            expectEquals (after->getFirstTailBlock(), 3);

            Stereo io;
            float* ch[2] = { io.left.data(), io.right.data() };
            std::fill (io.left.begin(), io.left.end(), 0.0f);
            expectEquals (chain->processFrom (chain->getFirstTailBlock(), ch, blockSize), 2);
            expectEquals (after->processFrom (after->getFirstTailBlock(), ch, blockSize), 2);
        }

        beginTest ("Without a merge it plays as a plain chain");
        {
            auto chain = std::make_unique<Chain>();
            chain->addBlock (std::make_unique<SplitBlock>());
            chain->addBlock (std::make_unique<TestGainBlock> (0.5f));
            chain->prepare (spec);
            expect (! chain->hasSplit());
            Stereo io;
            settle (*chain, io, 0.4f);
            expectWithinAbsoluteError (io.left.back(), 0.2f, 1.0e-5f);
        }

        beginTest ("Never allocates while playing, switching path B and moving the merge");
        {
            auto chain = makeSplit (list (std::make_unique<TestGainBlock> (1.0f, 32), std::make_unique<DelayBlock>()),
                                    list (std::make_unique<TestGainBlock> (0.7f)),
                                    list (std::make_unique<DelayBlock>()));
            auto& split = *chain->getBlocks()[1];
            auto* panB = chain->getBlocks()[4]->findParameter (mergeParams::panB);
            Stereo io;
            const auto before = realtimeAllocationCount();

            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 400; ++i)
                {
                    if (i % 37 == 0)
                        split.setEnabled (! split.isEnabled());

                    panB->setValue ((float) (i % 200) - 100.0f);
                    io.run (*chain, 0.3f);
                }
            }

            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static SplitPathTests splitPathTests;

} // namespace encore::test
