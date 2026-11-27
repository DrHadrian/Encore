// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2 step 7 (GATE-PLAN 11, 21, 23): the learned floor
// across a rebuild, the Gate pair, and today's gate as a Studio Gate.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/blocks/PedalGateBlock.h"
#include "engine/blocks/StudioGateBlock.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"
#include "session/PatchBuilder.h"

namespace encore::test
{

namespace
{
    class Pass final : public Block
    {
    public:
        Pass() : Block ("testPass", "Pass") {}

    private:
        void prepareBlock (const ProcessSpec&) override {}
        void resetBlock() noexcept override {}
        void processBlock (float* const*, int, int, const ProcessContext&) noexcept override {}
    };

    BlockModel model (const char* id, const char* type)
    {
        BlockModel b;
        b.id = id;
        b.type = type;
        return b;
    }

    std::function<juce::String()> counter()
    {
        auto n = std::make_shared<int> (0);
        return [n] { return "new" + juce::String (++*n); };
    }

    void apply (Block& block, const BlockModel& m)
    {
        for (const auto& [id, value] : m.params)
            if (auto* p = block.findParameter (id))
                p->setValue (value);
    }

    /** The gain curve a gate applies while keyed by the guitar: the block's
        own channel is 1.0, the guitar is the note, so the output is the gain. */
    std::vector<float> gainCurve (Block& gate, const std::vector<float>& guitar)
    {
        gate.prepare ({ sampleRate, blockSize });
        gate.reset();
        std::vector<float> out (guitar.size(), 1.0f);

        for (int offset = 0; offset < (int) out.size(); offset += blockSize)
        {
            const auto len = juce::jmin (blockSize, (int) out.size() - offset);
            float* ch[] = { out.data() + offset };
            ProcessContext context;
            context.chainInput = guitar.data() + offset;
            gate.process (ch, 1, len, context);
        }

        return out;
    }
}

class GateStateTests final : public juce::UnitTest
{
public:
    GateStateTests() : juce::UnitTest ("Gate state", "Encore") {}

    void runTest() override
    {
        beginTest ("The learned floor survives a rebuild, block by id");
        {
            Chain playing, rebuilt;

            for (auto* chain : { &playing, &rebuilt })
            {
                auto s2 = std::make_unique<PedalGateBlock>();
                s2->setModelId ("s2");
                chain->addBlock (std::move (s2));
                auto x4 = std::make_unique<StudioGateBlock>();
                x4->setModelId ("x4");
                chain->addBlock (std::move (x4));
                chain->prepare ({ sampleRate, blockSize });
            }

            // Only the S2 was in the playing chain under that id; a new one stays fresh.
            auto fresh = std::make_unique<PedalGateBlock>();
            fresh->setModelId ("fresh");
            rebuilt.addBlock (std::move (fresh));
            rebuilt.prepare ({ sampleRate, blockSize });

            dynamic_cast<GateBlock&> (*playing.getBlocks()[0]).setLearnedFloorDb (-55.0f);
            dynamic_cast<GateBlock&> (*playing.getBlocks()[1]).setLearnedFloorDb (-81.0f);
            carryLearnedState (playing, rebuilt);

            const auto floorOf = [&] (int i) { return dynamic_cast<GateBlock&> (*rebuilt.getBlocks()[(size_t) i]).getLearnedFloorDb(); };
            expectWithinAbsoluteError (floorOf (0), -55.0f, 1e-4f);
            expectWithinAbsoluteError (floorOf (1), -81.0f, 1e-4f);
            expectWithinAbsoluteError (floorOf (2), dsp::AdaptiveGate::initialFloorDb, 1e-4f, "a new gate starts from -70");

            // And it stays through a prepare (a device change rebuilds nothing).
            rebuilt.prepare ({ 96000.0, blockSize });
            expectWithinAbsoluteError (floorOf (0), -55.0f, 1e-4f);
        }

        beginTest ("The Gate pair: an S2 before the first amp (Soft, 3), one after it (Hard, Guitar), linked");
        {
            PatchModel patch;
            auto& blocks = patch.mainLane().blocks;
            blocks = { model ("in", PedalGateBlock::typeId), model ("amp", NamAmpBlock::typeId), model ("cab", "cabIr"),
                       model ("amp2", NamAmpBlock::typeId) };

            expect (addGatePair (patch, counter()));
            expectEquals ((int) blocks.size(), 6);
            expectEquals (blocks[1].id, juce::String ("new1"));
            expectEquals (blocks[2].id, juce::String ("amp"), "before the first amp only");
            expectEquals (blocks[3].id, juce::String ("new2"));

            const auto& before = blocks[1];
            const auto& after = blocks[3];
            expectEquals (before.type, juce::String (PedalGateBlock::typeId));
            expectEquals (before.params.at ("soft"), (float) PedalGateBlock::Mode::soft);
            expectEquals (before.params.at ("threshold"), 3.0f);
            expect (before.refs.empty());
            expectEquals (after.type, juce::String (PedalGateBlock::typeId));
            expectEquals (after.params.at ("soft"), (float) PedalGateBlock::Mode::hard);
            expectEquals (after.params.at ("detect"), (float) PedalGateBlock::Detect::guitar);
            expectEquals (after.refs.at ("linkTo"), juce::String ("new1"));

            // Built, the second follows the first.
            Chain chain;

            for (const auto& b : blocks)
            {
                std::unique_ptr<Block> block;

                if (b.type == PedalGateBlock::typeId)
                    block = std::make_unique<PedalGateBlock>();
                else
                    block = std::make_unique<Pass>();

                block->setModelId (b.id);
                block->setRefs (b.refs);
                apply (*block, b);
                chain.addBlock (std::move (block));
            }

            chain.prepare ({ sampleRate, blockSize });
            const auto& follower = dynamic_cast<GateBlock&> (*chain.getBlocks()[3]);
            expect (follower.getLinkState() == GateBlock::LinkState::following);
        }

        beginTest ("The Gate pair goes around an amp in path B; with no amp it's refused and nothing changes");
        {
            PatchModel patch;
            patch.mainLane().blocks = { model ("split", splitBlockType), model ("a", PedalGateBlock::typeId), model ("merge", mergeBlockType) };
            patch.lanes.emplace_back();
            patch.lanes[1].blocks = { model ("ampB", NamAmpBlock::typeId) };

            expect (addGatePair (patch, counter()));
            expectEquals ((int) patch.mainLane().blocks.size(), 3, "the main lane is untouched");
            const auto& b = patch.lanes[1].blocks;
            expectEquals ((int) b.size(), 3);
            expectEquals (b[1].id, juce::String ("ampB"));
            expectEquals (b[2].refs.at ("linkTo"), b[0].id);

            PatchModel noAmp;
            noAmp.mainLane().blocks = { model ("g", PedalGateBlock::typeId), model ("eq", "eq") };
            const auto before = noAmp.mainLane().blocks.size();
            expect (! addGatePair (noAmp, counter()));
            expectEquals (noAmp.mainLane().blocks.size(), before);
        }

        beginTest ("Today's gate as a Studio Gate: the values copy over, Detect guitar becomes Key");
        {
            auto gate = model ("g1", NoiseGateBlock::typeId);
            gate.params = { { "threshold", -48.0f }, { "release", 150.0f }, { "range", -40.0f }, { "detectGuitar", 0.0f } };
            gate.enabled = false;

            const auto x4 = convertOldGate (gate);
            expectEquals (x4.type, juce::String (StudioGateBlock::typeId));
            expectEquals (x4.id, juce::String ("g1"));
            expect (! x4.enabled, "on/off stays");
            expectEquals (x4.params.at ("threshold"), -48.0f);
            expectWithinAbsoluteError (x4.params.at ("release"), 150.0f * 40.0f / 8.6859f, 0.01f, "the same line in dB");
            expectEquals (x4.params.at ("range"), -40.0f);
            expectEquals (x4.params.at ("key"), (float) StudioGateBlock::Key::input);
            expect (x4.params.count ("detectGuitar") == 0);

            // An old show that never saved Detect guitar had it on.
            const auto defaults = convertOldGate (model ("g2", NoiseGateBlock::typeId));
            expectEquals (defaults.params.at ("key"), (float) StudioGateBlock::Key::guitar);
            expectWithinAbsoluteError (defaults.params.at ("release"), 80.0f * 90.0f / 8.6859f, 0.01f);

            // Capped to the X4's range.
            auto slow = model ("g3", NoiseGateBlock::typeId);
            slow.params = { { "release", 500.0f } };
            expectEquals (convertOldGate (slow).params.at ("release"), 2000.0f);

            // Anything else comes back unchanged.
            expectEquals (convertOldGate (model ("p", PedalGateBlock::typeId)).type, juce::String (PedalGateBlock::typeId));
        }

        beginTest ("Today's gate and its Studio Gate give the same gain curve within 1 dB after the first 2 ms");
        {
            const auto settle = (int) (0.002 * sampleRate);

            for (const auto& [label, note, values] :
                 { std::tuple { "pluck", pluck (48000 * 2), std::map<juce::String, float> {} },
                   std::tuple { "palm-mute", palmMute (48000), std::map<juce::String, float> { { "threshold", -50.0f }, { "release", 40.0f } } } })
            {
                auto old = model ("g", NoiseGateBlock::typeId);

                for (const auto& [id, v] : values)
                    old.params[id] = v;

                NoiseGateBlock today;
                StudioGateBlock x4;
                apply (today, old);
                apply (x4, convertOldGate (old));

                const auto a = gainCurve (today, note.signal);
                const auto b = gainCurve (x4, note.signal);
                auto worst = 0.0f;
                auto worstAt = 0;

                // Away from the opening (2 ms: the attack shapes differ by design),
                // and while either is still above -60 dB: below that both are
                // inaudible, and today's one-pole only creeps towards the range.
                for (int i = 0; i < (int) a.size(); ++i)
                {
                    if (i >= note.onset && i < note.onset + settle)
                        continue;

                    if (a[(size_t) i] < 0.001f && b[(size_t) i] < 0.001f)
                        continue;

                    const auto da = juce::Decibels::gainToDecibels (a[(size_t) i], -90.0f);
                    const auto db = juce::Decibels::gainToDecibels (b[(size_t) i], -90.0f);

                    if (std::abs (da - db) > worst)
                    {
                        worst = std::abs (da - db);
                        worstAt = i;
                    }
                }

                for (int ms : { 1, 5, 10, 20, 30, 50, 80, 99, 100, 101, 102, 103, 105, 110, 150, 300, 600, 1000, 1500 })
                    if (ms * 48 < (int) a.size())
                        logMessage ("  " + juce::String (ms) + " ms: " + juce::String (juce::Decibels::gainToDecibels (a[(size_t) ms * 48], -90.0f), 1)
                                    + " / " + juce::String (juce::Decibels::gainToDecibels (b[(size_t) ms * 48], -90.0f), 1));

                logMessage (juce::String (label) + ": worst difference " + juce::String (worst, 2) + " dB at "
                            + juce::String (1000.0 * worstAt / sampleRate, 1) + " ms");
                expect (worst <= 1.0f, label);
            }
        }

        beginTest ("The display feed: frames in order while it's on, none while it's off, no allocation");
        {
            PedalGateBlock s2;
            s2.prepare ({ sampleRate, blockSize });
            s2.reset();

            // A level that rises 1 dB every 10 ms, from -60 dBFS.
            const auto n = 48000;
            std::vector<float> in ((size_t) n);

            for (int i = 0; i < n; ++i)
                in[(size_t) i] = juce::Decibels::decibelsToGain (-60.0f + (float) (i / 480)) * (i % 2 == 0 ? 1.0f : -1.0f);

            std::vector<float> buffer ((size_t) blockSize);
            const auto play = [&] (int from, int to)
            {
                for (int offset = from; offset < to; offset += blockSize)
                {
                    std::copy (in.begin() + offset, in.begin() + offset + blockSize, buffer.begin());
                    float* ch[] = { buffer.data() };
                    s2.process (ch, 1, blockSize, {});
                }
            };

            play (0, 9600);   // off: nothing
            GateBlock::DisplayFrame frame;
            expect (! s2.popDisplayFrame (frame), "no frames while off");

            s2.setDisplayActive (true);
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;
                play (9600, 9600 + 48 * 64 * 5);   // 320 ms: 64 frames of 5 ms
            }
            expectEquals (realtimeAllocationCount() - before, 0);

            int frames = 0;
            auto last = -200.0f, worstStep = 0.0f;

            while (s2.popDisplayFrame (frame))
            {
                if (frames > 0)
                    worstStep = juce::jmax (worstStep, last - frame.inputDb);   // never falls

                last = frame.inputDb;
                ++frames;
            }

            expectEquals (frames, 64);
            expect (worstStep <= 0.01f, "in order");
            expectWithinAbsoluteError (last, -60.0f + (9600 + 48 * 64 * 5 - 1) / 480, 1.01f, "the last frame's peak");

            s2.setDisplayActive (false);
            play (9600 + 48 * 64 * 5, 48000 - blockSize);
            expect (! s2.popDisplayFrame (frame), "none after it's switched off");
        }
    }
};

static GateStateTests gateStateTests;

} // namespace encore::test
