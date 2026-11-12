// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_core/juce_core.h>

#include <cmath>

#include "app/AppSettings.h"
#include "model/Routing.h"
#include "model/ShowJson.h"

namespace encore::test
{

/** Milestone 11's model: this computer's inputs and outputs, a patch's input,
    sends and split paths, and how they're saved. */
class RoutingModelTests final : public juce::UnitTest
{
public:
    RoutingModelTests() : juce::UnitTest ("Routing model", "Encore") {}

    static RoutingConfig makeRig()
    {
        RoutingConfig rig;
        rig.inputs = { { "Guitar", 1 }, { "Acoustic", 2 } };

        RoutingOutput foh;
        foh.name = "Front of house";
        foh.channels = { 1, 2 };
        foh.eq.lowCut = 80.0f;
        foh.eq.mid = 1.5f;
        foh.delayMs = 12.0f;

        RoutingOutput iem;
        iem.name = "In-ear";
        iem.channels = { 3, 4 };
        iem.levelDb = -6.0f;

        RoutingOutput di;
        di.name = "Amp DI";
        di.channels = { 5 };
        di.mute = true;

        rig.outputs = { foh, iem, di };
        return rig;
    }

    void runTest() override
    {
        beginTest ("Defaults play exactly as before routing");
        {
            const auto rig = makeDefaultRouting (3);
            expectEquals ((int) rig.inputs.size(), 1);
            expectEquals (rig.inputs[0].name, juce::String ("Guitar"));
            expectEquals (rig.inputs[0].channel, 3);
            expectEquals ((int) rig.outputs.size(), 1);
            expectEquals (rig.outputs[0].name, juce::String ("Main"));
            expect (rig.outputs[0].isAllOutputs());
            expect (rig.outputs[0].eq.isFlat());
            expectEquals (rig.outputs[0].levelDb, 0.0f);
            expect (! rig.outputs[0].mute);
        }

        beginTest ("Names are matched ignoring case; a missing input plays the first");
        {
            const auto rig = makeRig();
            expect (rig.findOutput ("in-EAR ") != nullptr);
            expectEquals (rig.indexOfOutput ("Amp DI"), 2);
            expect (rig.findOutput ("Monitor wedge") == nullptr);
            expectEquals (rig.inputFor ("acoustic").channel, 2);
            expectEquals (rig.inputFor ("Mic").name, juce::String ("Guitar"));
            expectEquals (rig.inputFor ({}).name, juce::String ("Guitar"));
            expectEquals (rig.uniqueOutputName ("in-ear"), juce::String ("in-ear 2"));
            expectEquals (rig.uniqueInputName ("Bass"), juce::String ("Bass"));
        }

        beginTest ("Round trip through the computer's settings format");
        {
            auto rig = makeRig();
            rig.outputs[1].levelDb = routing::silentDb;   // -inf has no JSON number
            const auto text = juce::JSON::toString (routingToVar (rig));
            expect (text.contains ("\"off\""));
            expect (routingFromVar (juce::JSON::parse (text)) == rig);
        }

        beginTest ("A damaged or hand-edited file always gives a usable routing");
        {
            const auto parsed = routingFromVar (juce::JSON::parse (R"({
                "inputs": [ { "name": "", "channel": -4 }, { "name": "guitar", "channel": 900 }, { "name": "GUITAR", "channel": 2 } ],
                "outputs": [ { "name": "A", "channels": [ 3, 3, 4, 5 ], "level": 99, "delayMs": 1000,
                               "eq": { "lowCut": 1, "highCut": 1e9, "bass": -80, "midFreq": "x" } },
                             { "name": "a", "channels": [] },
                             { "name": "B", "channels": "all", "level": "off", "mute": true },
                             { "name": "C" }, { "name": "D" }, { "name": "E" }, { "name": "F" }, { "name": "G" },
                             { "name": "H" }, { "name": "I" } ]
            })"));

            expectEquals ((int) parsed.inputs.size(), 3);
            expectEquals (parsed.inputs[0].name, juce::String ("Input"));
            expectEquals (parsed.inputs[0].channel, 1);
            expectEquals (parsed.inputs[1].channel, routing::maxChannel);
            expectEquals (parsed.inputs[2].name, juce::String ("GUITAR 2"), "names stay unique");

            expectEquals ((int) parsed.outputs.size(), routing::maxOutputs);
            const auto& a = parsed.outputs[0];
            expect (a.channels == std::vector<int> { 3, 4 }, "at most a pair, no channel twice");
            expectEquals (a.levelDb, routing::maxLevelDb);
            expectEquals (a.delayMs, routing::maxDelayMs);
            expectEquals (a.eq.lowCut, OutputEq::minLowCut);
            expectEquals (a.eq.highCut, OutputEq::maxHighCut);
            expectEquals (a.eq.bass, -OutputEq::maxGainDb);
            expectEquals (a.eq.midFreq, OutputEq().midFreq);
            expect (parsed.outputs[1].channels == std::vector<int> { 1 }, "an empty list isn't \"all\"");
            expectEquals (parsed.outputs[1].name, juce::String ("a 2"));
            expect (parsed.outputs[2].isAllOutputs());
            expectEquals (parsed.outputs[2].levelDb, routing::silentDb);
            expect (parsed.outputs[2].mute);

            const auto empty = routingFromVar (juce::JSON::parse ("{ \"inputs\": [], \"outputs\": 5 }"), 2);
            expect (empty == makeDefaultRouting (2));
            expect (routingFromVar (juce::var ("nonsense")) == makeDefaultRouting());
        }

        beginTest ("Names: trimmed, one line, at most 32 characters");
        {
            expectEquals (routing::cleanName ("  In\near  "), juce::String ("In ear"));
            expectEquals (routing::cleanName (juce::String::repeatedString ("x", 50)).length(), routing::maxNameLength);
            RoutingConfig rig;
            RoutingOutput longName;
            longName.name = juce::String::repeatedString ("y", 32);
            rig.outputs.push_back (longName);
            expectEquals (rig.uniqueOutputName (juce::String::repeatedString ("y", 40)).length(), routing::maxNameLength);
        }

        beginTest ("Levels, gains and words");
        {
            expectEquals (routing::gainFor (routing::silentDb), 0.0f);
            expectEquals (routing::gainFor (-1000.0f), 0.0f);
            expectEquals (routing::gainFor (std::numeric_limits<float>::quiet_NaN()), 0.0f);
            expectWithinAbsoluteError (routing::gainFor (0.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (routing::gainFor (60.0f), std::pow (10.0f, 6.0f / 20.0f), 1.0e-5f);
            expectEquals (routing::clampLevel (-std::numeric_limits<float>::infinity()), routing::silentDb);

            const auto rig = makeRig();
            const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
            expectEquals (describeChannels (rig.outputs[0]), "1/2" + dot + "stereo");
            expectEquals (describeChannels (rig.outputs[2]), "5" + dot + "mono");
            expectEquals (describeChannels (makeDefaultRouting().outputs[0]), juce::String ("all outputs"));
            expectEquals (describeDelay (12.0f), juce::String ("12.0 ms"));
            expectEquals (describeDistance (12.0f), juce::String (juce::CharPointer_UTF8 ("\xe2\x89\x88 4.12 m")));
        }

        beginTest ("The computer's settings: migrates the guitar input, keeps it in step");
        {
            juce::TemporaryFile folder;
            folder.getFile().createDirectory();

            {
                AppSettings settings (folder.getFile());
                settings.setGuitarInputChannel (2);   // 0-based: input 3
                const auto migrated = settings.getRouting();
                expectEquals (migrated.inputs[0].channel, 3);
                expect (migrated.outputs[0].isAllOutputs());
                expect (! settings.getOutputStripsFull());

                auto rig = makeRig();
                rig.inputs[0].channel = 4;
                settings.setRouting (rig);
                settings.setOutputStripsFull (true);
                expectEquals (settings.getGuitarInputChannel(), 3, "older code reading the guitar input keeps working");
                settings.save();
            }

            AppSettings reopened (folder.getFile());
            auto expected = makeRig();
            expected.inputs[0].channel = 4;
            expect (reopened.getRouting() == expected);
            expect (reopened.getOutputStripsFull());
            folder.getFile().deleteRecursively();
        }
    }
};

/** A patch's input, sends and split in the show file (schema 3). */
class RoutingShowTests final : public juce::UnitTest
{
public:
    RoutingShowTests() : juce::UnitTest ("Routing in the show file", "Encore") {}

    static PatchModel makeDualAmp()
    {
        auto patch = makeDefaultPatch ("Dual amp");
        patch.mainLane().input = "Guitar";
        insertSplit (patch, 0);
        auto& main = patch.mainLane();
        // split, amp, cab (path A), merge, gate, eq; path B: an amp.
        auto amp = main.blocks[2];
        auto cab = main.blocks[3];
        main.blocks.erase (main.blocks.begin() + 2, main.blocks.begin() + 4);
        main.blocks.insert (main.blocks.begin() + 1, { amp, cab });
        patch.lanes[1].blocks = { duplicatePatch (patch, "x").mainLane().blocks[1] };
        patch.setSend ("Front of house", 0.0f, true);
        patch.setSend ("In-ear", -6.0f, true);
        patch.setSend ("Amp DI", 0.0f, false);
        return patch;
    }

    void runTest() override
    {
        beginTest ("Sends: listed by name, an unlisted output gets 0 dB");
        {
            auto patch = makeDefaultPatch ("Clean");
            expect (patch.sendFor ("In-ear") == SendModel { "In-ear", 0.0f, true });
            patch.setSend ("In-ear", -6.0f, true);
            patch.setSend ("in-ear", -9.0f, true);
            expectEquals ((int) patch.sends.size(), 1, "same name, any case: one send");
            expectEquals (patch.sendFor ("IN-EAR").levelDb, -9.0f);
            patch.setSend ("Amp DI", routing::silentDb, true);
            expect (! patch.sendFor ("Amp DI").on, "-inf is off");
            patch.setSend ("Wedge", 40.0f, true);
            expectEquals (patch.sendFor ("Wedge").levelDb, routing::maxLevelDb);
        }

        beginTest ("Split and merge: one each, path B in lanes[1]");
        {
            auto patch = makeDefaultPatch ("P");
            expect (! patch.hasSplit());
            expect (insertSplit (patch, 1));
            expect (! insertSplit (patch, 0), "one split per patch");
            expectEquals (patch.splitIndex(), 1);
            expectEquals (patch.mergeIndex(), 2);
            expectEquals ((int) patch.lanes.size(), 2);
            expect (patch.pathB() != nullptr);

            const auto& merge = patch.mainLane().blocks[2];
            expectEquals (merge.params.at (mergeParams::levelA), 0.0f);
            expectEquals (merge.params.at (mergeParams::panB), 0.0f);

            removeSplit (patch);
            expect (! patch.hasSplit());
            expectEquals ((int) patch.lanes.size(), 1);
            expectEquals ((int) patch.mainLane().blocks.size(), 4, "path A's blocks stay");
        }

        beginTest ("A malformed split is made whole");
        {
            auto block = [] (const char* type) { BlockModel b; b.id = newId(); b.type = type; return b; };

            PatchModel noMerge;
            noMerge.lanes = { LaneModel { {}, {}, { block ("gate"), block ("split"), block ("nam") } } };
            normaliseSplit (noMerge);
            expectEquals (noMerge.mergeIndex(), 3);
            expectEquals ((int) noMerge.lanes.size(), 2);

            PatchModel mess;
            mess.lanes = { LaneModel { {}, {}, { block ("merge"), block ("split"), block ("eq"), block ("split"),
                                                 block ("merge"), block ("merge") } },
                           LaneModel { {}, {}, { block ("split"), block ("nam"), block ("merge") } },
                           LaneModel { {}, {}, { block ("cab") } } };
            normaliseSplit (mess);
            expectEquals ((int) mess.mainLane().blocks.size(), 3);
            expectEquals (mess.splitIndex(), 0);
            expectEquals (mess.mergeIndex(), 2);
            expectEquals ((int) mess.lanes.size(), 2);
            expectEquals ((int) mess.lanes[1].blocks.size(), 1);
            expectEquals (mess.lanes[1].blocks[0].type, juce::String ("nam"));

            PatchModel orphan;
            orphan.lanes = { LaneModel { {}, {}, { block ("merge"), block ("eq") } }, LaneModel {} };
            normaliseSplit (orphan);
            expectEquals ((int) orphan.mainLane().blocks.size(), 1);
            expectEquals ((int) orphan.lanes.size(), 1);
        }

        beginTest ("Schema: 3 only when routing is used, so older Encore keeps opening the rest");
        {
            auto show = makeDefaultShow ("Gig");
            expectEquals (show.requiredSchemaVersion(), 1);
            expect (json::toJson (show).contains ("\"schemaVersion\": 1"));

            auto withInput = show;
            withInput.patches[0].mainLane().input = "Acoustic";
            expectEquals (withInput.requiredSchemaVersion(), 3);

            auto withSend = show;
            withSend.patches[0].setSend ("In-ear", -3.0f, true);
            expectEquals (withSend.requiredSchemaVersion(), 3);

            auto withSplit = show;
            auto cue = RunItem::makeCue ("1", "Intro", {});
            cue.uniquePatch = makeDualAmp();
            withSplit.runningOrder.children.push_back (cue);
            expectEquals (withSplit.requiredSchemaVersion(), 3, "a cue's own patch counts too");
            expect (json::toJson (withSplit).contains ("\"schemaVersion\": 3"));

            juce::String error;
            expect (json::fromJson (R"({ "format": "encore-show", "schemaVersion": 3 })", error).has_value());
            expect (! json::fromJson (R"({ "format": "encore-show", "schemaVersion": 4 })", error).has_value());
        }

        beginTest ("Round trip: input, sends (with off and -inf), split, merge, path B");
        {
            auto show = makeDefaultShow ("Gig");
            show.patches.push_back (makeDualAmp());
            show.patches[1].setSend ("Monitor wedge", routing::silentDb, true);
            auto cue = RunItem::makeCue ("2", "Dual amp intro", show.patches[1].id);
            show.runningOrder.children.push_back (cue);

            const auto text = json::toJson (show);
            juce::String error;
            const auto loaded = json::fromJson (text, error);
            expect (loaded.has_value(), error);
            expect (*loaded == show);

            const auto& patch = loaded->patches[1];
            expectEquals (patch.inputName(), juce::String ("Guitar"));
            expect (! patch.sendFor ("Amp DI").on);
            expectEquals (patch.sendFor ("Amp DI").levelDb, 0.0f, "an off send keeps its level");
            expect (! patch.sendFor ("Monitor wedge").on);
            expect (patch.hasSplit());
            expectEquals ((int) patch.pathB()->blocks.size(), 1);

            expect (loaded->inputNamesUsed() == juce::StringArray { "Guitar" });
            expect (loaded->outputNamesUsed() == juce::StringArray { "Front of house", "In-ear", "Amp DI", "Monitor wedge" });
        }

        beginTest ("Reading sends: damaged entries are skipped, the first of a name wins");
        {
            juce::String error;
            const auto show = json::fromJson (R"({ "format": "encore-show", "schemaVersion": 3,
                "patches": [ { "id": "p", "name": "P", "lanes": [ { "blocks": [] } ],
                    "sends": [ { "output": "" }, { "level": 3 }, { "output": "IEM", "level": -3 },
                               { "output": "iem", "level": -20 }, { "output": "FOH", "level": "loud" },
                               { "output": "DI", "level": -500 }, 7 ] } ] })", error);

            expect (show.has_value(), error);
            const auto& sends = show->patches[0].sends;
            expectEquals ((int) sends.size(), 3);
            expectEquals (show->patches[0].sendFor ("IEM").levelDb, -3.0f);
            expectEquals (show->patches[0].sendFor ("FOH").levelDb, 0.0f);
            expect (! show->patches[0].sendFor ("DI").on);
        }

        beginTest ("Duplicating a split patch gives every block (both paths) a new ID");
        {
            const auto original = makeDualAmp();
            const auto copy = duplicatePatch (original, "Copy");
            expect (copy.sends == original.sends);
            expect (copy.pathB()->blocks[0].id != original.pathB()->blocks[0].id);
            expect (copy.mainLane().blocks[0].id != original.mainLane().blocks[0].id);
        }
    }
};

static RoutingModelTests routingModelTests;
static RoutingShowTests routingShowTests;

} // namespace encore::test
