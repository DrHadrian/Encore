// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B4: "When CPU is tight" (EQ-PLAN §2.6): the engine's
// rule and the Studio EQ's "Make it lighter".

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "engine/core/CpuTight.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    using Eq = StudioEqBlock;

    void set (Block& b, const juce::String& id, float v) { b.findParameter (id)->setValue (v); }

    /** Plays `seconds` of a quiet tone; `tight` says what the engine passes. */
    void run (Eq& eq, double seconds, bool tight)
    {
        std::array<float, blockSize> x {};   // on the stack: no allocation in the realtime test
        ProcessContext context;
        context.cpuTight = tight;

        for (int n = 0; n < (int) (seconds * sampleRate / blockSize); ++n)
        {
            float* ch[] = { x.data() };
            eq.process (ch, 1, blockSize, context);
        }
    }

    void setUp (Eq& eq, Eq::CpuTight setting, Eq::Character character, Eq::Quality q)
    {
        set (eq, "cpuTight", (float) setting);
        set (eq, "character", (float) character);
        set (eq, "quality", (float) q);
        eq.prepare (spec);
        eq.reset();
    }
}

class CpuTightTests final : public juce::UnitTest
{
public:
    CpuTightTests() : juce::UnitTest ("CPU tight", "Encore") {}

    void runTest() override
    {
        beginTest ("The rule: over 85 % for 8 buffers in a row, or an overrun");
        {
            CpuTightRule rule;

            for (int i = 0; i < 7; ++i)
                expect (! rule.update (0.9), "buffer " + juce::String (i + 1));

            expect (rule.update (0.9), "the 8th");
            expect (rule.update (0.86), "still");
            expect (! rule.update (0.5), "one easy buffer ends it");

            for (int i = 0; i < 7; ++i)
                rule.update (0.9);

            expect (! rule.update (0.84), "85 % or less doesn't count");
            expect (rule.update (1.2), "an overrun, at once");
            expect (! rule.update (0.3), "... for that buffer only");
            rule.update (0.95);
            rule.reset();
            expect (! rule.isTight());
        }

        beginTest ("Make it lighter: a tight stretch steps Quality to Live and stays until Back to full");
        {
            Eq eq;
            setUp (eq, Eq::CpuTight::makeItLighter, Eq::Character::warm, Eq::Quality::high);
            run (eq, 0.1, false);
            const auto full = eq.getLatencySamples();
            expect (! eq.isLighter());

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;
                run (eq, 0.05, true);
            }
            expectEquals (realtimeAllocationCount() - before, 0, "no allocation");
            expectEquals (eq.getLighterSteps(), (int) Eq::qualityLive);
            run (eq, 0.05, false);

            dsp::Oversampler live;
            live.prepare (1, blockSize, sampleRate);
            live.resetTo (dsp::Quality::live);
            expectEquals (eq.getLatencySamples(), live.getLatencySamples(), "Live's latency");
            expectEquals (eq.findParameter ("quality")->getChoiceIndex(), (int) Eq::Quality::high, "the setting isn't changed (or saved)");

            run (eq, 5.0, false);
            expect (eq.isLighter(), "no automatic return");

            eq.backToFull();
            run (eq, 0.1, false);
            expect (! eq.isLighter());
            expectEquals (eq.getLatencySamples(), full, "High again");

            // Still tight after Back to full: it steps down again.
            run (eq, 0.05, true);
            expect (eq.isLighter());
        }

        beginTest ("Keep the sound never changes; with nothing to lighten the block isn't marked");
        {
            Eq keep;
            setUp (keep, Eq::CpuTight::keepTheSound, Eq::Character::warm, Eq::Quality::max);
            run (keep, 0.1, false);
            const auto latency = keep.getLatencySamples();
            run (keep, 3.0, true);
            expect (! keep.isLighter());
            expectEquals (keep.getLatencySamples(), latency);

            // Clean (Quality plays no part), or Warm already at Live; Spectral
            // and Linear phase change nothing until B5.
            Eq clean;
            setUp (clean, Eq::CpuTight::makeItLighter, Eq::Character::clean, Eq::Quality::max);
            set (clean, "b01Used", 1.0f);
            set (clean, "b01Mode", (float) Eq::Mode::spectral);
            set (clean, "phase", (float) Eq::Phase::linear);
            run (clean, 3.0, true);
            expect (! clean.isLighter(), "Clean: nothing to lighten");

            Eq atLive;
            setUp (atLive, Eq::CpuTight::makeItLighter, Eq::Character::warm, Eq::Quality::live);
            run (atLive, 3.0, true);
            expect (! atLive.isLighter(), "already Live");
        }
    }
};

static CpuTightTests cpuTightTests;

} // namespace encore::test
