// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2 step 6: linked gates (GATE-PLAN §4). A follower
// shares an earlier gate's detection and keeps its own Range and Release.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalGateBlock.h"
#include "engine/blocks/StudioGateBlock.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    /** Delays by a fixed latency it reports (as a plugin would). */
    class Latency final : public Block
    {
    public:
        explicit Latency (int samples) : Block ("testLatency", "Latency"), delay (samples) { setModelId ("latency"); }
        int getLatencySamples() const noexcept override { return delay; }

    private:
        void prepareBlock (const ProcessSpec&) override { line.assign ((size_t) delay + 1, 0.0f); }
        void resetBlock() noexcept override { std::fill (line.begin(), line.end(), 0.0f); }
        void processBlock (float* const* ch, int n, int len, const ProcessContext&) noexcept override
        {
            const auto size = (int) line.size();

            for (int i = 0; i < len; ++i)
            {
                line[(size_t) write] = ch[0][i];
                ch[0][i] = line[(size_t) ((write + 1) % size)];
                write = (write + 1) % size;

                for (int c = 1; c < n; ++c)
                    ch[c][i] = ch[0][i];
            }
        }

        int delay, write = 0;
        std::vector<float> line;
    };

    template <typename Gate>
    Gate& add (Chain& chain, const char* id, const char* linkTo = nullptr)
    {
        auto gate = std::make_unique<Gate>();
        gate->setModelId (id);

        if (linkTo != nullptr)
            gate->setRefs ({ { "linkTo", linkTo } });

        return static_cast<Gate&> (chain.addBlock (std::move (gate)));
    }

    /** Palm-mutes and a ringing note over a -70 dB noise floor. */
    std::vector<float> playing()
    {
        auto a = palmMute (48000 * 2, 4800, 0.5f, -70.0f, 2);
        auto b = palmMute (48000 * 2, 24000, 0.4f, -90.0f, 3);
        auto c = pluck (48000 * 2, 48000, 0.4f, 1200.0, -90.0f, 98.0, 6, 4);

        for (size_t i = 0; i < a.signal.size(); ++i)
            a.signal[i] += b.signal[i] + c.signal[i];

        return a.signal;
    }

    struct Trace
    {
        std::vector<bool> leader, follower;
        std::vector<float> leaderReduction, followerReduction;
    };

    /** Sample by sample, so the open states can be compared per sample. */
    template <typename L, typename F>
    Trace trace (Chain& chain, L& leader, F& follower, const std::vector<float>& signal, int from = 0, int to = -1)
    {
        Trace t;
        to = to < 0 ? (int) signal.size() : to;

        for (int i = from; i < to; ++i)
        {
            float left = signal[(size_t) i], right = 0.0f;
            float* ch[2] = { &left, &right };
            chain.process (ch, 1);
            t.leader.push_back (leader.isOpen());
            t.follower.push_back (follower.isOpen());
            t.leaderReduction.push_back (leader.getGainReductionDb());
            t.followerReduction.push_back (follower.getGainReductionDb());
        }

        return t;
    }

    int changes (const std::vector<bool>& v)
    {
        int n = 0;

        for (size_t i = 1; i < v.size(); ++i)
            n += v[i] != v[i - 1] ? 1 : 0;

        return n;
    }
}

class GateLinkTests final : public juce::UnitTest
{
public:
    GateLinkTests() : juce::UnitTest ("Gate links", "Encore") {}

    void runTest() override
    {
        const auto signal = playing();

        beginTest ("A linked pair opens and closes on the same sample (S2 -> S2, S2 -> X4, X4 -> S2)");
        {
            {
                Chain chain;
                auto& lead = add<PedalGateBlock> (chain, "lead");
                auto& follow = add<PedalGateBlock> (chain, "follow", "lead");
                chain.prepare (spec);
                chain.reset();
                lead.setLearnedFloorDb (-70.0f);
                follow.setLearnedFloorDb (-40.0f);   // its own floor doesn't matter: the leader's threshold does
                expect (follow.getLinkState() == GateBlock::LinkState::following);

                const auto t = trace (chain, lead, follow, signal);
                expect (changes (t.leader) >= 4, "the leader opens and closes on the playing");
                expect (t.leader == t.follower, "the follower on the same samples");
            }

            {
                Chain chain;
                auto& lead = add<PedalGateBlock> (chain, "lead");
                auto& follow = add<StudioGateBlock> (chain, "x4", "lead");
                follow.findParameter ("threshold")->setValue (-10.0f);   // ignored while it follows
                chain.prepare (spec);
                chain.reset();
                lead.setLearnedFloorDb (-70.0f);
                const auto t = trace (chain, lead, follow, signal);
                expect (t.leader == t.follower, "an X4 follows an S2");
            }

            {
                Chain chain;
                auto& lead = add<StudioGateBlock> (chain, "x4");
                lead.findParameter ("key")->setValue ((float) StudioGateBlock::Key::input);
                auto& follow = add<PedalGateBlock> (chain, "s2", "x4");
                chain.prepare (spec);
                chain.reset();
                const auto t = trace (chain, lead, follow, signal);
                expect (changes (t.leader) >= 4);
                expect (t.leader == t.follower, "an S2 follows an X4");
            }
        }

        beginTest ("With a latency between them: still the same sample of the signal");
        {
            Chain chain;
            auto& lead = add<PedalGateBlock> (chain, "lead");
            chain.addBlock (std::make_unique<Latency> (64));
            auto& follow = add<PedalGateBlock> (chain, "follow", "lead");
            chain.prepare (spec);
            chain.reset();
            lead.setLearnedFloorDb (-70.0f);
            const auto t = trace (chain, lead, follow, signal);

            int mismatches = 0;

            for (size_t i = 64; i < t.leader.size(); ++i)
                mismatches += t.follower[i] != t.leader[i - 64] ? 1 : 0;

            expectEquals (mismatches, 0, "the follower's decision at sample i is the leader's at i - 64");
            expect (changes (t.follower) >= 4);
        }

        beginTest ("The leader's own look-ahead needs nothing extra");
        {
            Chain chain;
            auto& lead = add<StudioGateBlock> (chain, "x4");
            lead.findParameter ("key")->setValue ((float) StudioGateBlock::Key::input);
            lead.findParameter ("lookahead")->setValue (1.0f);
            auto& follow = add<StudioGateBlock> (chain, "x4b", "x4");
            follow.findParameter ("lookahead")->setValue (1.0f);
            chain.prepare (spec);
            chain.reset();
            expectEquals (lead.getLatencySamples(), 48);
            expectEquals (follow.getLatencySamples(), 0, "a follower's own look-ahead is ignored");
            const auto t = trace (chain, lead, follow, signal);
            expect (t.leader == t.follower, "its decisions already line up with its delayed output");
        }

        beginTest ("Each keeps its own Range and Release");
        {
            Chain chain;
            auto& lead = add<PedalGateBlock> (chain, "lead");
            lead.findParameter ("decay")->setValue (0.0f);   // 20 ms base
            auto& follow = add<PedalGateBlock> (chain, "follow", "lead");
            follow.findParameter ("soft")->setValue ((float) PedalGateBlock::Mode::soft);
            follow.findParameter ("decay")->setValue (10.0f);   // 800 ms base
            chain.prepare (spec);
            chain.reset();
            lead.setLearnedFloorDb (-70.0f);

            auto note = pluck (48000 * 3, 4800, 0.5f, 300.0, -70.0f);
            const auto t = trace (chain, lead, follow, note.signal);
            const auto end = t.leader.size() - 1;
            expect (! t.leader[end] && ! t.follower[end], "both closed at the end");
            expect (t.leaderReduction[end] > 80.0f, "the leader mutes (Hard)");
            // Soft: the expander 1:2 on the shared level, 9 dB under the shared threshold.
            expect (t.followerReduction[end] > 5.0f && t.followerReduction[end] <= 15.01f,
                    "the follower turns down gently (Soft, at most 15 dB): " + juce::String (t.followerReduction[end]));

            // Shortly after closing, the slow follower has come down much less.
            size_t closedAt = 0;

            for (size_t i = 1; i < t.leader.size(); ++i)
                if (t.leader[i - 1] && ! t.leader[i])
                    closedAt = i;

            const auto later = closedAt + 480;   // 10 ms
            expect (t.leaderReduction[later] > 2.0f * t.followerReduction[later] + 1.0f,
                    "own release: " + juce::String (t.leaderReduction[later]) + " vs " + juce::String (t.followerReduction[later]));
        }

        beginTest ("Leader off: the follower falls back to its own detector, then links again");
        {
            Chain chain;
            auto& lead = add<PedalGateBlock> (chain, "lead");
            auto& follow = add<PedalGateBlock> (chain, "follow", "lead");
            chain.prepare (spec);
            chain.reset();
            lead.setLearnedFloorDb (-70.0f);
            follow.setLearnedFloorDb (-70.0f);

            std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
            auto runFor = [&] (int blocks)
            {
                for (int k = 0; k < blocks; ++k)
                {
                    std::fill (left.begin(), left.end(), 0.01f);
                    float* ch[2] = { left.data(), right.data() };
                    chain.process (ch, blockSize);
                }
            };

            runFor (10);
            expect (follow.getLinkState() == GateBlock::LinkState::following);
            expect (follow.getLinkInfo().isEmpty());

            lead.setEnabled (false);
            runFor (20);   // past the 10 ms bypass fade: the leader sleeps
            expect (follow.getLinkState() == GateBlock::LinkState::leaderOff);
            expectEquals (follow.getLinkInfo(), juce::String ("Link: leader off"));
            expect (follow.isOpen(), "its own detector: -40 dB is above its threshold");

            lead.setEnabled (true);
            runFor (10);
            expect (follow.getLinkState() == GateBlock::LinkState::following, "linked again");
            expectWithinAbsoluteError (left.back(), 0.01f, 1.0e-6f, "and still open, no gap");
        }

        beginTest ("A link to a later gate, a missing one or a non-gate is refused at build, with the info text");
        {
            Chain chain;
            auto& early = add<PedalGateBlock> (chain, "early", "late");
            add<PedalGateBlock> (chain, "late");
            auto& missing = add<PedalGateBlock> (chain, "lost", "nowhere");
            auto& self = add<PedalGateBlock> (chain, "self", "self");
            chain.addBlock (std::make_unique<Latency> (0));
            auto& notGate = add<PedalGateBlock> (chain, "ng", "latency");
            chain.prepare (spec);

            expect (early.getLinkState() == GateBlock::LinkState::refused);
            expectEquals (early.getLinkInfo(), juce::String ("Link: that gate isn't before this one"));
            expectEquals (missing.getLinkInfo(), juce::String ("Link: that gate isn't in this patch"));
            expectEquals (self.getLinkInfo(), juce::String ("Link: that gate isn't before this one"));
            expectEquals (notGate.getLinkInfo(), juce::String ("Link: that block isn't a gate"));
        }

        beginTest ("No allocation with links, the leader switching on and off");
        {
            Chain chain;
            auto& lead = add<PedalGateBlock> (chain, "lead");
            chain.addBlock (std::make_unique<Latency> (32));
            add<StudioGateBlock> (chain, "f1", "lead");
            add<PedalGateBlock> (chain, "f2", "f1");
            chain.prepare (spec);
            std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int k = 0; k < 400; ++k)
                {
                    if (k % 50 == 0)
                        lead.setEnabled (! lead.isEnabled());

                    std::copy (signal.begin() + (k % 600) * blockSize, signal.begin() + (k % 600 + 1) * blockSize, left.begin());
                    float* ch[2] = { left.data(), right.data() };
                    chain.process (ch, blockSize);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static GateLinkTests gateLinkTests;

} // namespace encore::test
