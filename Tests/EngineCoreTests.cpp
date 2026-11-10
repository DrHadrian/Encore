// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/core/ChainHandover.h"
#include "engine/core/LockFreeFifo.h"
#include "engine/core/Realtime.h"
#include "engine/dsp/SafetyLimiter.h"

namespace encore::test
{

class LockFreeFifoTests final : public juce::UnitTest
{
public:
    LockFreeFifoTests() : juce::UnitTest ("Lock-free FIFO", "Encore") {}

    void runTest() override
    {
        beginTest ("Holds exactly its capacity, in order");
        LockFreeFifo<int, 4> fifo;

        for (int i = 1; i <= 4; ++i)
            expect (fifo.push (i));

        expect (! fifo.push (5), "full queue refuses");

        int value = 0;

        for (int i = 1; i <= 4; ++i)
        {
            expect (fifo.pop (value));
            expectEquals (value, i);
        }

        expect (! fifo.pop (value), "empty queue returns nothing");

        beginTest ("Wraps around");

        for (int round = 0; round < 10; ++round)
        {
            expect (fifo.push (round) && fifo.push (round + 100));
            expect (fifo.pop (value) && value == round);
            expect (fifo.pop (value) && value == round + 100);
        }
    }
};

class ParameterTests final : public juce::UnitTest
{
public:
    ParameterTests() : juce::UnitTest ("Parameters", "Encore") {}

    void runTest() override
    {
        Parameter p ({ "gain", "Gain", { -12.0f, 12.0f }, 0.0f, "dB", 1, 0.02 });

        beginTest ("Values are clamped to the range");
        p.setValue (40.0f);
        expectEquals (p.getValue(), 12.0f);
        p.setValue (-40.0f);
        expectEquals (p.getValue(), -12.0f);

        beginTest ("Audio side ramps to a new value in the ramp time");
        p.setValue (0.0f);
        p.prepare (sampleRate);
        p.setValue (6.0f);
        p.beginBlock();
        expect (p.isSmoothing());
        p.skip ((int) (0.02 * sampleRate) - 1);
        expect (p.currentValue() > 5.9f && p.currentValue() < 6.0f, "still ramping just before the end");
        p.skip (2);
        expectEquals (p.currentValue(), 6.0f);
        expect (! p.isSmoothing());
    }
};

class ChainHandoverTests final : public juce::UnitTest
{
public:
    ChainHandoverTests() : juce::UnitTest ("Chain handover", "Encore") {}

    void runTest() override
    {
        beginTest ("Newest chain wins, older ones are freed on the message thread");

        ChainHandover handover;
        handover.prepareAll (spec);

        auto makeChain = []
        {
            auto c = std::make_unique<Chain>();
            c->addBlock (std::make_unique<EqBlock>());
            return c;
        };

        auto first = makeChain();
        auto* firstRaw = first.get();
        handover.publish (std::move (first));
        expect (handover.acquireActive() == firstRaw);

        auto second = makeChain();
        auto third = makeChain();
        auto* thirdRaw = third.get();
        handover.publish (std::move (second));
        handover.publish (std::move (third));
        expectEquals (handover.getNumOwnedChains(), 3);

        expect (handover.acquireActive() == thirdRaw, "skips straight to the newest");
        expect (handover.getLatest() == thirdRaw);

        handover.collectGarbage();
        expectEquals (handover.getNumOwnedChains(), 1, "the two replaced chains are gone");

        beginTest ("Published chains are prepared with the current spec");
        expect (thirdRaw->getSpec() == spec);
    }
};

class RealtimeSafetyTests final : public juce::UnitTest
{
public:
    RealtimeSafetyTests() : juce::UnitTest ("Real-time safety", "Encore") {}

    void runTest() override
    {
        beginTest ("The allocation counter itself works");
        {
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;
                auto* p = new int (42);
                delete p;
            }
            expectEquals (realtimeAllocationCount() - before, 2, "one new + one delete counted");
        }

        beginTest ("Processing, bypassing, sweeping and swapping chains never allocates");
        {
            ChainHandover handover;
            handover.prepareAll (spec);

            auto chain = std::make_unique<Chain>();
            auto& gate = chain->addBlock (std::make_unique<NoiseGateBlock>());
            auto& eq = chain->addBlock (std::make_unique<EqBlock>());
            handover.publish (std::move (chain));

            dsp::SafetyLimiter limiter;
            limiter.prepare (sampleRate);

            auto signal = sine (220.0, 2.0f, blockSize);   // loud enough to hit the limiter
            const auto before = realtimeAllocationCount();

            for (int i = 0; i < 2000; ++i)
            {
                // UI-side changes between callbacks (atomics only).
                eq.findParameter ("midFreq")->setValue (200.0f + (float) (i * 37 % 4800));
                eq.findParameter ("bass")->setValue (i % 2 == 0 ? 12.0f : -12.0f);
                gate.setEnabled (i % 50 < 25);
                eq.setEnabled (i % 70 < 50);

                if (i == 1000)   // a chain swap mid-run (publishing is message-thread work)
                {
                    auto replacement = std::make_unique<Chain>();
                    replacement->addBlock (std::make_unique<EqBlock>());
                    handover.publish (std::move (replacement));
                }

                // What the audio callback does.
                const rt::ScopedRealtimeSection realtime;
                float* channels[1] = { signal.data() };

                if (auto* active = handover.acquireActive())
                    active->process (channels, 1, blockSize);

                limiter.process (channels, 1, blockSize);
            }

            expectEquals (realtimeAllocationCount() - before, 0);
            handover.collectGarbage();
        }
    }
};

static LockFreeFifoTests lockFreeFifoTests;
static ParameterTests parameterTests;
static ChainHandoverTests chainHandoverTests;
static RealtimeSafetyTests realtimeSafetyTests;

} // namespace encore::test
