// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3: the Studio EQ's model side (EQ-PLAN §2.8, §4, §5;
// MODULES-PLAN §3.1): bands that come and go.

#include "TestSignals.h"

#include "engine/blocks/StudioEqBlock.h"
#include "model/ShowJson.h"
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

        beginTest ("A / B: switch plays the other set (B starts as a copy of A); copy either way; only parameter values");
        {
            auto eq = emptyEq();
            juce::String error;
            addEqBand (eq, bell (400.0, 3.0), error);
            eq.expression["b01Gain"] = { 1, -30.0f, 30.0f };
            eq.enabled = false;
            const auto a = eq.params;

            expect (! eq.compare.isUsed(), "never used: nothing extra");
            expect (switchCompare (eq));
            expect (eq.compare.bPlaying && eq.params == a, "B is a copy of A at first");

            eq.params["b01Gain"] = -5.0f;   // an edit to B
            expect (switchCompare (eq));
            expect (! eq.compare.bPlaying);
            expectEquals (eq.params.at ("b01Gain"), 3.0f, "A again");
            expectEquals (eq.compare.other.at ("b01Gain"), -5.0f, "B kept");
            expect (eq.expression.count ("b01Gain") == 1 && ! eq.enabled, "on/off and expression never move");

            expect (copyCompare (eq, true));   // A to B
            expectEquals (eq.compare.other.at ("b01Gain"), 3.0f);
            eq.compare.other["b01Freq"] = 900.0f;
            expect (copyCompare (eq, false));  // B to A: A is playing, so params change
            expectEquals (eq.params.at ("b01Freq"), 900.0f);

            // Heard at once on a live block.
            Eq live;
            applyParameters (eq, live);
            expectEquals (live.getBand (0).frequency, 900.0);
        }

        beginTest ("A / B on a Pedal EQ, the same way");
        {
            BlockModel g7;
            g7.id = "g7";
            g7.type = "pedalEq";
            g7.params["g800"] = 6.0f;
            expect (switchCompare (g7));
            g7.params["g800"] = -4.0f;
            expect (switchCompare (g7));
            expectEquals (g7.params.at ("g800"), 6.0f);
            expect (copyCompare (g7, true));
            expectEquals (g7.compare.other.at ("g800"), 6.0f);
        }

        beginTest ("A / B is saved (schema 5) and read back; on a block that isn't an FX Unit it's ignored");
        {
            auto show = makeDefaultShow ("AB");
            auto eq = emptyEq();
            juce::String error;
            addEqBand (eq, bell (400.0, 3.0), error);
            switchCompare (eq);
            eq.params["b01Gain"] = 7.0f;
            show.patches[0].mainLane().blocks.push_back (eq);

            const auto text = json::toJson (show);
            expect (text.contains ("\"compare\""));
            const auto back = json::fromJson (text, error);
            expect (back.has_value(), error);

            if (back)
            {
                const auto& b = back->patches[0].mainLane().blocks.back();
                expect (b.compare.bPlaying);
                expectEquals (b.params.at ("b01Gain"), 7.0f);
                expectEquals (b.compare.other.at ("b01Gain"), 3.0f);
                expect (b == eq, "the round trip keeps everything");
            }

            // A show without A/B writes no compare at all.
            expect (! json::toJson (makeDefaultShow ("Plain")).contains ("\"compare\""));

            // Today's EQ (not converted) with A/B data in its file: ignored, and refused as an edit.
            auto old = makeDefaultShow ("Old");
            auto& oldEq = old.patches[0].mainLane().blocks[3];   // Amp -> Cab -> Gate -> EQ
            expectEquals (oldEq.type, juce::String ("eq"));
            oldEq.compare.other["bass"] = 6.0f;
            const auto oldBack = json::fromJson (json::toJson (old), error);
            expect (oldBack.has_value() && ! oldBack->patches[0].mainLane().blocks[3].compare.isUsed());
            expect (! switchCompare (oldEq) && ! copyCompare (oldEq, true));
        }
    }
};

static StudioEqModelTests studioEqModelTests;

} // namespace encore::test
