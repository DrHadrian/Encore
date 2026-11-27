// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3: the Studio EQ's model side (EQ-PLAN §2.8, §4, §5;
// MODULES-PLAN §3.1): bands that come and go.

#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "session/PatchBuilder.h"

namespace encore::test
{

namespace
{
    using Eq = StudioEqBlock;

    Eq::BandSettings bell (double f, double gain)
    {
        Eq::BandSettings b;
        b.shape = Eq::Shape::bell;
        b.frequency = f;
        b.gainDb = gain;
        return b;
    }

    BlockModel emptyEq()
    {
        BlockModel m;
        m.id = "q24";
        m.type = Eq::typeId;
        return m;
    }
}

class StudioEqModelTests final : public juce::UnitTest
{
public:
    StudioEqModelTests() : juce::UnitTest ("Studio EQ model", "Encore") {}

    void runTest() override
    {
        beginTest ("Adding takes the lowest free slot; the 25th is refused");
        {
            auto eq = emptyEq();
            juce::String error;

            for (int i = 0; i < Eq::numSlots; ++i)
                expectEquals (addEqBand (eq, bell (100.0 + i, 1.0), error), i);

            expectEquals (addEqBand (eq, bell (5000.0, 1.0), error), -1);
            expectEquals (error, juce::String ("All 24 bands are in use"));
            expectEquals (eq.params.at ("b07Freq"), 106.0f);
            expectEquals (eq.params.at ("b07Used"), 1.0f);
        }

        beginTest ("Removing frees the slot and resets it; the others keep their numbers; a new band refills the gap");
        {
            auto eq = emptyEq();
            juce::String error;

            for (int i = 0; i < 5; ++i)
                addEqBand (eq, bell (200.0 * (i + 1), 2.0 + i), error);

            eq.params["b03Place"] = 2.0f;   // a B4 setting on the slot goes too
            removeEqBand (eq, 2);           // band 3
            expectEquals (eq.params.at ("b03Used"), 0.0f);
            expectEquals (eq.params.at ("b03Freq"), Eq::defaultFrequency);
            expectEquals (eq.params.at ("b03Gain"), 0.0f);
            expectEquals (eq.params.at ("b03Place"), 0.0f);
            expectEquals (eq.params.at ("b04Freq"), 800.0f, "band 4 is still band 4");
            expectEquals (eq.params.at ("b04Used"), 1.0f);

            expectEquals (addEqBand (eq, bell (3000.0, -4.0), error), 2, "the lowest free slot: 3 again");
            expectEquals (addEqBand (eq, bell (4000.0, -4.0), error), 5);
        }

        beginTest ("Removing a band drops every expression assignment on its slot, only those");
        {
            auto eq = emptyEq();
            juce::String error;
            addEqBand (eq, bell (300.0, 3.0), error);
            addEqBand (eq, bell (900.0, 3.0), error);
            eq.expression["b01Gain"] = { 1, -30.0f, 30.0f };
            eq.expression["b01Freq"] = { 2, 10.0f, 30000.0f };
            eq.expression["b02Gain"] = { 1, -30.0f, 30.0f };
            eq.expression["output"] = { 3, -36.0f, 36.0f };

            expectEquals (removeEqBand (eq, 0), 2);
            expect (eq.expression.count ("b01Gain") == 0 && eq.expression.count ("b01Freq") == 0);
            expect (eq.expression.count ("b02Gain") == 1 && eq.expression.count ("output") == 1);
            expectEquals (removeEqBand (eq, 0), 0, "nothing left on it");
        }

        beginTest ("The edit is heard at once: a live block takes the slot");
        {
            auto eq = emptyEq();
            Eq live;
            live.prepare ({ sampleRate, blockSize });
            juce::String error;
            const auto slot = addEqBand (eq, bell (2500.0, 6.0), error);
            applyEqSlot (eq, live, slot);
            expectEquals (live.getNumBandsInUse(), 1);
            expectEquals (live.getBand (slot).frequency, 2500.0);
            expectEquals (live.getBand (slot).gainDb, 6.0);

            removeEqBand (eq, slot);
            applyEqSlot (eq, live, slot);
            expectEquals (live.getNumBandsInUse(), 0);
            expectEquals (live.getBand (slot).gainDb, 0.0);
        }
    }
};

static StudioEqModelTests studioEqModelTests;

} // namespace encore::test
