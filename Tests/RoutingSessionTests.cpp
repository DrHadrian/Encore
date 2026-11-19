// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "FakeDevice.h"
#include "TestSignals.h"

#include <juce_events/juce_events.h>

#include "engine/blocks/SplitBlocks.h"
#include "model/MidiMapping.h"
#include "session/PatchBuilder.h"
#include "session/PreShowCheck.h"
#include "session/ShowSession.h"

namespace encore::test
{

/** Milestone 11 in the session: split patches built and captured, block
    edits across lanes, a patch's input and sends, "Mute output" on a
    footswitch, and the pre-show check's routing lines. */
class RoutingSessionTests final : public juce::UnitTest
{
public:
    RoutingSessionTests() : juce::UnitTest ("Routing in the session", "Encore") {}

    static void pumpUntil (AudioEngine& engine, std::function<bool()> done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            playOneBlock (engine);
            engine.collectGarbage();
        }
    }

    static void playOneBlock (AudioEngine& engine)
    {
        std::vector<float> in ((size_t) blockSize, 0.0f), outL ((size_t) blockSize), outR ((size_t) blockSize);
        const float* inputs[] = { in.data() };
        float* outputs[] = { outL.data(), outR.data() };
        engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, {});
    }

    static PatchModel makeSplitPatch()
    {
        auto patch = makeDefaultPatch ("Dual amp");   // amp, cab, gate, eq
        insertSplit (patch, 2);                       // amp, cab, split, merge, gate, eq
        auto& main = patch.mainLane().blocks;
        std::swap (main[3], main[4]);                 // amp, cab, split, gate (A), merge, eq
        BlockModel delay;
        delay.id = newId();
        delay.type = "delay";
        patch.pathB()->blocks.push_back (delay);
        patch.mainLane().blocks[4].params[mergeParams::panA] = -100.0f;
        patch.mainLane().blocks[4].params[mergeParams::levelB] = routing::silentDb;   // below the knob's range: off
        return patch;
    }

    static int indexOfType (const PatchModel& patch, const juce::String& type)
    {
        for (int i = 0; i < patch.numBlocks(); ++i)
            if (patch.blockAt (i)->type == type)
                return i;

        return -1;
    }

    void runTest() override
    {
        juce::MessageManager::getInstance();
        const auto chainSpec = ProcessSpec { sampleRate, blockSize };

        beginTest ("Building a split patch: both lanes, block indices as the model's");
        {
            const auto patch = makeSplitPatch();
            juce::StringArray problems;
            auto chain = buildChain (patch, {}, chainSpec, problems);

            expect (problems.isEmpty(), problems.joinIntoString ("; "));
            expect (chain->hasSplit());
            expectEquals (chain->getNumMainBlocks(), 6);
            expectEquals ((int) chain->getBlocks().size(), patch.numBlocks());

            for (int i = 0; i < patch.numBlocks(); ++i)
                expectEquals (chain->getBlocks()[(size_t) i]->getModelId(), patch.blockAt (i)->id);

            auto& merge = *chain->getBlocks()[4];
            expectEquals (merge.findParameter (mergeParams::panA)->getValue(), -100.0f);
            expectEquals (merge.findParameter (mergeParams::levelB)->getValue(), MergeBlock::minLevelDb, "off stays off");

            // Capture gives the same lanes back, with live tweaks.
            chain->getBlocks()[6]->findParameter ("mix")->setValue (55.0f);
            auto captured = patch;
            captureChain (*chain, captured, {});
            expectEquals ((int) captured.lanes.size(), 2);
            expectEquals ((int) captured.pathB()->blocks.size(), 1);
            expectEquals (captured.pathB()->blocks[0].params.at ("mix"), 55.0f);
            expectEquals (captured.mainLane().blocks[4].type, juce::String (mergeBlockType));
        }

        beginTest ("Capturing an older chain keeps the patch's newer structure");
        {
            auto patch = makeDefaultPatch ("P");
            juce::StringArray problems;
            auto chain = buildChain (patch, {}, chainSpec, problems);
            chain->getBlocks()[3]->findParameter ("treble")->setValue (5.0f);

            insertSplit (patch, 1);   // an edit whose rebuild hasn't arrived
            captureChain (*chain, patch, {});
            expect (patch.hasSplit(), "the split isn't lost");
            expectEquals (patch.numBlocks(), 6);
            expectEquals (patch.mainLane().blocks[5].params.at ("treble"), 5.0f, "the tweak is kept");
        }

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);

        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-routing-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        beginTest ("Split edits: add, move between paths, nothing passes the split, removing the merge removes it");
        {
            ShowSession session (engine);
            expect (session.newShow (temp.getChildFile ("Split.encore"), "Split").wasOk());
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            const auto key = session.getActivePatchId();
            auto patch = [&] { return *session.getPatch (key); };

            expect (session.addSplit (2));
            expect (! session.addSplit (0), "one split per patch");
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            expect (patch().hasSplit());
            expectEquals (engine.getChain()->getSplitIndex(), 2);

            // Into path B, at its start.
            session.addBlock ("delay", 0, 1);
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            expectEquals ((int) patch().pathB()->blocks.size(), 1);
            expectEquals ((int) engine.getChain()->getBlocks().size(), 7);

            // Path B's delay to path A, and back.
            session.moveBlockToPath (6, false);
            expectEquals ((int) patch().pathB()->blocks.size(), 0);
            expectEquals (patch().mainLane().blocks[3].type, juce::String ("delay"), "at the end of path A");
            session.moveBlockToPath (3, true);
            expectEquals ((int) patch().pathB()->blocks.size(), 1);

            // The amp can't jump over the split; the split doesn't move.
            const auto before = patch().mainLane().blocks;
            session.moveBlock (1, 2);
            session.moveBlock (2, 1);
            expect (patch().mainLane().blocks == before);

            // Stomp on the split: path B on a footswitch.
            session.setBlockStomp (2, 3);
            expectEquals (session.getBlockStomp (2), 3);
            session.setBlockStomp (6, 4);
            expectEquals (patch().pathB()->blocks[0].stomp, 4, "path B blocks by the chain's index");

            session.removeBlock (indexOfType (patch(), mergeBlockType));
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            expect (! patch().hasSplit());
            expectEquals (patch().numBlocks(), 4, "path A's blocks stay, path B's go");
            expect (! engine.getChain()->hasSplit());
        }

        beginTest ("A patch's input and sends act at once, by name");
        {
            RoutingConfig rig;
            rig.inputs = { { "Guitar", 1 }, { "Acoustic", 2 } };
            RoutingOutput foh, iem;
            foh.name = "Front of house";
            foh.channels = { 1, 2 };
            iem.name = "In-ear";
            iem.channels = { 3, 4 };
            rig.outputs = { foh, iem };
            engine.setRouting (rig);

            ShowSession session (engine);
            expect (session.newShow (temp.getChildFile ("Sends.encore"), "Sends").wasOk());
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            const auto key = session.getActivePatchId();
            auto* chain = engine.getChain();
            expect (chain != nullptr);

            const auto iemSlot = engine.getOutputSlot ("In-ear");
            expectEquals (chain->getSendGain (iemSlot), 1.0f, "not mentioned: 0 dB");

            session.setPatchSend (key, "In-ear", -6.0f, true);
            expectWithinAbsoluteError (chain->getSendGain (iemSlot), routing::gainFor (-6.0f), 1.0e-6f);
            session.setPatchSend (key, "In-ear", -6.0f, false);
            expectEquals (chain->getSendGain (iemSlot), 0.0f);
            expect (session.hasUnsavedChanges());

            session.setPatchInput (key, "acoustic");
            expectEquals (chain->getInputSlot(), engine.getInputSlot ("Acoustic"));
            session.setPatchInput (key, "Mic");
            expectEquals (chain->getInputSlot(), engine.getInputSlot ("Guitar"), "not here: the first");

            // The computer's routing changes: patches follow their names.
            std::swap (rig.outputs[0], rig.outputs[1]);
            rig.outputs[1].name = "PA";
            engine.setRouting (rig);
            session.refreshRouting();
            expectEquals (chain->getSendGain (engine.getOutputSlot ("In-ear")), 0.0f, "still off");
            expectEquals (chain->getSendGain (engine.getOutputSlot ("PA")), 1.0f);

            // Saved, schema 3.
            expect (session.save().wasOk());
            const auto text = temp.getChildFile ("Sends.encore/show.json").loadFileAsString();
            expect (text.contains ("\"schemaVersion\": 3"));
            expect (text.contains ("\"sends\""));

            engine.setRouting (makeDefaultRouting());
        }

        beginTest ("\"Mute output\" on a footswitch: by name, a toggle, nothing when it isn't here");
        {
            RoutingConfig rig = makeDefaultRouting();
            RoutingOutput iem;
            iem.name = "In-ear";
            iem.channels = { 2 };
            rig.outputs.push_back (iem);
            engine.setRouting (rig);

            auto& router = engine.getMidiRouter();
            MidiBinding mute { MidiAction::muteOutput, 1, { MidiTrigger::Kind::controlChange, 0, 20 }, "in-ear" };
            MidiBinding wedge { MidiAction::muteOutput, 1, { MidiTrigger::Kind::controlChange, 0, 21 }, "Wedge" };
            router.setBindings ({ mute, wedge });

            const auto slot = engine.getOutputSlot ("In-ear");
            router.receive (0xb0, 20, 127);
            playOneBlock (engine);
            expect (engine.isOutputMuted (slot));
            router.receive (0xb0, 20, 0);
            router.receive (0xb0, 21, 127);
            playOneBlock (engine);
            expect (engine.isOutputMuted (slot), "Wedge isn't here: nothing");

            expect (engine.syncOutputMutes());
            expect (engine.getRouting().findOutput ("In-ear")->mute, "kept in the routing (saved)");
            expect (! engine.syncOutputMutes());

            router.receive (0xb0, 20, 127);
            playOneBlock (engine);
            expect (! engine.isOutputMuted (slot));

            // The output moves to another slot: the binding follows its name.
            rig.outputs.insert (rig.outputs.begin(), iem);
            rig.outputs[0].name = "Side fill";
            rig.outputs[2].name = "In-ear 2";
            engine.setRouting (rig);
            expectEquals (engine.getOutputSlot ("In-ear"), -1);
            router.receive (0xb0, 20, 0);
            router.receive (0xb0, 20, 127);
            playOneBlock (engine);

            for (int s = 0; s < routing::maxOutputs; ++s)
                expect (! engine.isOutputMuted (s), "a renamed output isn't muted by the old name");

            // Saved and read back with its name.
            const auto read = bindingsFromVar (bindingsToVar ({ mute }));
            expectEquals ((int) read.size(), 1);
            expectEquals (read[0].output, juce::String ("in-ear"));
            expectEquals (describe (read[0]), juce::String ("Mute output: in-ear"));
            expect (bindingsFromVar (juce::JSON::parse (R"([ { "action": "muteOutput", "kind": "cc", "number": 3 } ])")).empty(),
                    "muting nothing isn't read");

            router.setBindings (makeDefaultMidiBindings());
            engine.setRouting (makeDefaultRouting());
        }

        beginTest ("Pre-show check: the routing lines, worded as designed");
        {
            PreShowFacts f;
            f.deviceOpen = true;
            f.callbacksRunning = true;
            f.deviceType = "ASIO";
            f.deviceName = "Focusrite USB ASIO";
            f.bufferSize = 64;
            f.sampleRate = 48000.0;
            f.deviceInputChannels = 2;
            f.deviceOutputChannels = 4;
            f.midiInputsEnabled = { "FCB1010" };
            f.midiInputsConnected = { "FCB1010" };
            f.hasNextCueMapping = true;
            f.numCues = 3;

            RoutingConfig rig;
            rig.inputs = { { "Guitar", 1 } };
            RoutingOutput main, iem, di;
            main.name = "Main";
            main.channels = { 1, 2 };
            iem.name = "In-ear";
            iem.channels = { 1, 2 };
            di.name = "Amp DI";
            di.channels = { 5 };
            di.mute = true;
            rig.outputs = { main, iem, di };
            f.routing = rig;

            f.patchRouting = { { "Dual amp", "Mic", { { "Monitor wedge", 0.0f, true } } },
                               { "Lead", "", { { "Monitor wedge", -3.0f, true }, { "Amp DI", 0.0f, false } } },
                               { "Ambient", "", { { "Main", 0.0f, false }, { "In-ear", 0.0f, false } } },
                               { "Clean", "", { { "Monitor wedge", 0.0f, false } } } };

            const auto results = runPreShowChecks (f);
            auto find = [&] (const juce::String& title) -> const PreShowCheckResult*
            {
                for (auto& r : results)
                    if (r.title == title)
                        return &r;

                return nullptr;
            };

            const auto q = [] (const char* quotedName)
            {
                return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + quotedName + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d"));
            };

            using Status = PreShowCheckResult::Status;
            using Fix = PreShowCheckResult::Fix;

            if (auto* r = find ("Output " + q ("Monitor wedge") + " isn't on this computer"); expect (r != nullptr), r != nullptr)
            {
                expect (r->status == Status::problem && r->fix == Fix::routing);
                expectEquals (r->detail, juce::String ("Dual amp and Lead send to it. Add it in Routing, or send those patches somewhere else."),
                              "Clean's send is off: it doesn't count");
            }

            if (auto* r = find ("Input " + q ("Mic") + " isn't on this computer"); expect (r != nullptr), r != nullptr)
                expectEquals (r->detail, juce::String ("Dual amp plays Guitar (input 1) instead."));

            if (auto* r = find ("Output " + q ("Amp DI") + " needs output 5"); expect (r != nullptr), r != nullptr)
                expectEquals (r->detail, juce::String ("Focusrite USB ASIO has 4 outputs."));

            if (auto* r = find ("Output " + q ("Amp DI") + " is muted"); expect (r != nullptr), r != nullptr)
            {
                expect (r->status == Status::warning && r->fix == Fix::unmuteOutput);
                expectEquals (r->target, juce::String ("Amp DI"));
                expectEquals (r->detail, juce::String ("Unmute it before the show if it should play."));
            }

            if (auto* r = find (q ("Ambient") + " sends to nothing"); expect (r != nullptr), r != nullptr)
                expectEquals (r->detail, juce::String ("Every send is off or goes to a muted output."));

            if (auto* r = find ("Outputs share a channel"); expect (r != nullptr), r != nullptr)
                expectEquals (r->detail, q ("Main") + " and " + q ("In-ear") + " both use output 1.");

            expect (find ("Routing") == nullptr, "no all-clear while something's wrong");
            expect (results.front().status == Status::problem, "worst first");

            // All fixed: one quiet line.
            PreShowFacts good = f;
            good.routing.outputs[1].channels = { 3, 4 };
            good.routing.outputs.pop_back();
            good.routing.inputs.push_back ({ "Mic", 2 });
            good.patchRouting = { { "Clean", "Mic", {} } };
            const auto fine = runPreShowChecks (good);
            int routingLines = 0;

            for (auto& r : fine)
                if (r.title.contains ("Output") || r.title.contains ("Input") || r.title.contains ("nothing") || r.title.contains ("share"))
                    ++routingLines;

            expectEquals (routingLines, 0);
            const PreShowCheckResult* ok = nullptr;

            for (auto& r : fine)
                if (r.title == "Routing")
                    ok = &r;

            if (expect (ok != nullptr), ok != nullptr)
                expectEquals (ok->detail, juce::String ("2 inputs, 2 outputs, all on this interface"));

            // The defaults say nothing (nothing new for a show without routing).
            PreShowFacts plain = f;
            plain.routing = makeDefaultRouting();
            plain.patchRouting = { { "Clean", "", {} } };

            for (auto& r : runPreShowChecks (plain))
                expect (! r.title.contains ("Routing") && ! r.title.contains ("Output ") && ! r.title.contains ("Input "), r.title);

            // A channel the interface has but that is switched off.
            PreShowFacts off = good;
            off.switchedOnOutputs = 0b0011;
            bool found = false;

            for (auto& r : runPreShowChecks (off))
                found = found || (r.title == "Output " + q ("In-ear") + " uses output 3, which is switched off" && r.fix == Fix::audioSettings);

            expect (found);

            expectEquals (joinNames ({ "A" }), juce::String ("A"));
            expectEquals (joinNames ({ "A", "B", "C" }), juce::String ("A, B and C"));
        }

        engine.audioDeviceStopped();
        temp.deleteRecursively();
    }
};

static RoutingSessionTests routingSessionTests;

} // namespace encore::test
