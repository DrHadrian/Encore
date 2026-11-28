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

        beginTest ("The B5 steps, in order: Spectral as Dynamic, Linear phase as Zero latency, Quality to Live; Back to full restores all");
        {
            for (auto method : { Eq::SpectralMethod::subBands, Eq::SpectralMethod::fft })
            {
                Eq eq;
                set (eq, "phase", (float) Eq::Phase::linear);
                set (eq, "b01Used", 1.0f);
                set (eq, "b01Mode", (float) Eq::Mode::spectral);
                set (eq, "b01Range", -6.0f);
                set (eq, "spectralMethod", (float) method);
                setUp (eq, Eq::CpuTight::makeItLighter, Eq::Character::warm, Eq::Quality::high);
                run (eq, 0.2, false);
                const auto full = eq.getLatencySamples();
                expectGreaterThan (full, 576, "Linear Medium plus the character");
                expect (eq.isLinearPlaying());

                run (eq, 0.05, true);
                expectEquals (eq.getLighterSteps(), (int) Eq::spectralAsDynamic, "first: Spectral as Dynamic");
                expect (eq.modeOf (0) == Eq::Mode::dynamic);
                run (eq, 0.5, true);
                expectEquals (eq.getLighterSteps(), (int) Eq::spectralAsDynamic, "a second before the next step");
                run (eq, 0.6, true);
                expectEquals (eq.getLighterSteps(), (int) (Eq::spectralAsDynamic | Eq::linearAsZeroLatency), "then Zero latency");
                run (eq, 0.2, false);
                expect (! eq.isLinearPlaying());
                const auto lighter = eq.getLatencySamples();
                expectLessThan (lighter, 100, "the latency follows: only the character's");
                run (eq, 1.2, true);
                expectEquals (eq.getLighterSteps(), (int) (Eq::spectralAsDynamic | Eq::linearAsZeroLatency | Eq::qualityLive),
                              "then Quality");

                eq.backToFull();
                run (eq, 0.5, false);
                expect (! eq.isLighter());
                expect (eq.modeOf (0) == Eq::Mode::spectral);
                expect (eq.isLinearPlaying());
                expectEquals (eq.getLatencySamples(), full, "all back");
            }

            // FFT in Zero latency: the first step takes its stage (and latency) away.
            Eq stage;
            set (stage, "b01Used", 1.0f);
            set (stage, "b01Mode", (float) Eq::Mode::spectral);
            set (stage, "b01Range", -6.0f);
            set (stage, "spectralMethod", (float) Eq::SpectralMethod::fft);
            setUp (stage, Eq::CpuTight::makeItLighter, Eq::Character::clean, Eq::Quality::live);
            run (stage, 0.2, false);
            expectEquals (stage.getLatencySamples(), 576, "the FFT stage");
            run (stage, 0.05, true);
            run (stage, 0.2, false);
            expectEquals (stage.getLighterSteps(), (int) Eq::spectralAsDynamic);
            expectEquals (stage.getLatencySamples(), 0, "no stage");
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

            // Clean (Quality plays no part), or Warm already at Live, in Zero
            // latency with no spectral band.
            Eq clean;
            setUp (clean, Eq::CpuTight::makeItLighter, Eq::Character::clean, Eq::Quality::max);
            set (clean, "b01Used", 1.0f);
            set (clean, "b01Mode", (float) Eq::Mode::dynamic);
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
