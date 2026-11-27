// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B0 (decision 0062): hidden block types, and the show
// schema of shows that use a new module.

#include "TestSignals.h"

#include "engine/blocks/EngineProbeBlock.h"
#include "engine/blocks/PedalEqBlock.h"
#include "model/ShowJson.h"
#include "session/PatchBuilder.h"

namespace encore::test
{

class HiddenBlockTypeTests final : public juce::UnitTest
{
public:
    HiddenBlockTypeTests() : juce::UnitTest ("Hidden block types", "Encore") {}

    static const BlockType* findType (const std::vector<BlockType>& types, const juce::String& id)
    {
        for (const auto& t : types)
            if (t.id == id)
                return &t;

        return nullptr;
    }

    void runTest() override
    {
        beginTest ("A hidden type is known but not offered in the Add block menu");
        {
            const auto* probe = findType (getBlockTypes(), EngineProbeBlock::typeId);
            expect (probe != nullptr && probe->hidden);

            const auto menu = getMenuBlockTypes();
            expect (findType (menu, EngineProbeBlock::typeId) == nullptr);

            for (const auto& t : menu)
                expect (! t.hidden, t.id);

            // The menu players see is what it was before the module base.
            juce::StringArray ids;

            for (const auto& t : menu)
                ids.add (t.id);

            expectEquals (ids.joinIntoString (","), juce::String ("nam,cab,gate,eq,delay"));
        }

        beginTest ("createBlock still makes a hidden type");
        {
            auto block = createBlock (EngineProbeBlock::typeId);
            expect (block != nullptr);
            expect (block != nullptr && block->getTypeId() == EngineProbeBlock::typeId);
        }

        beginTest ("A hidden type loads from a show and plays");
        {
            auto show = makeDefaultShow ("Hidden");
            auto& patch = show.patches[0];
            BlockModel probe;
            probe.id = "probe-1";
            probe.type = EngineProbeBlock::typeId;
            probe.params["trim"] = -6.0f;
            patch.mainLane().blocks.push_back (probe);

            juce::String error;
            const auto loaded = json::fromJson (json::toJson (show), error);
            expect (loaded.has_value(), error);

            if (! loaded)
                return;

            juce::StringArray problems;
            auto chain = buildChain (loaded->patches[0], ShowFolder(), spec, problems);
            expect (problems.isEmpty(), problems.joinIntoString ("; "));

            const auto& blocks = chain->getBlocks();
            expect (! blocks.empty() && blocks.back()->getTypeId() == EngineProbeBlock::typeId);

            if (! blocks.empty())
                expectEquals (blocks.back()->findParameter ("trim")->getValue(), -6.0f);
        }

        beginTest ("The test unit at 0 dB is bit-exact passthrough");
        {
            EngineProbeBlock probe;
            probe.prepare (spec);
            const auto input = noise (0.5f, 4800);
            expect (runMono (probe, input) == input);
        }
    }
};

class NewModuleSchemaTests final : public juce::UnitTest
{
public:
    NewModuleSchemaTests() : juce::UnitTest ("Schema 5 (new modules)", "Encore") {}

    static BlockModel probeBlock()
    {
        BlockModel b;
        b.id = "probe-1";
        b.type = EngineProbeBlock::typeId;
        return b;
    }

    void runTest() override
    {
        beginTest ("Only types marked as new modules count");
        {
            expect (isNewModuleBlockType (EngineProbeBlock::typeId));
            expect (isNewModuleBlockType (PedalEqBlock::typeId));

            for (const auto& t : getBlockTypes())
                if (t.id != EngineProbeBlock::typeId && t.id != PedalEqBlock::typeId)
                    expect (! isNewModuleBlockType (t.id), t.id);

            expect (! isNewModuleBlockType ("noSuchType"));
            expectEquals (ShowModel::currentSchemaVersion, 5);
        }

        beginTest ("Without a new module a show keeps the version it needs today");
        {
            const auto show = makeDefaultShow ("Old");
            expectEquals (show.requiredSchemaVersion(), 1);
            expect (json::toJson (show).contains ("\"schemaVersion\": 1"));
        }

        beginTest ("With a new module it is saved as schema 5 and opens again here");
        {
            auto show = makeDefaultShow ("New");
            show.patches[0].mainLane().blocks.push_back (probeBlock());
            expectEquals (show.requiredSchemaVersion(), 5);

            const auto text = json::toJson (show);
            expect (text.contains ("\"schemaVersion\": 5"));

            juce::String error;
            const auto back = json::fromJson (text, error);
            expect (back.has_value(), error);
            expect (back.has_value() && back->requiredSchemaVersion() == 5);

            // A block in a cue's own patch counts too.
            auto cueOnly = makeDefaultShow ("Cue");
            PatchModel own = cueOnly.patches[0];
            own.mainLane().blocks.push_back (probeBlock());
            auto cue = RunItem::makeCue ("cue-own", "Own patch", {});
            cue.uniquePatch = own;
            cueOnly.runningOrder.children.push_back (cue);
            expectEquals (cueOnly.requiredSchemaVersion(), 5);
        }

        beginTest ("An older Encore refuses it with its \"newer Encore\" message");
        {
            // Encore before this change read up to schema 4: what it does with a
            // newer show is what this one does with one newer than 5.
            auto show = makeDefaultShow ("Newer");
            show.patches[0].mainLane().blocks.push_back (probeBlock());
            const auto newer = json::toJson (show).replace ("\"schemaVersion\": 5",
                                                           "\"schemaVersion\": " + juce::String (ShowModel::currentSchemaVersion + 1));
            juce::String error;
            expect (! json::fromJson (newer, error).has_value());
            expect (error.contains ("newer version of Encore"), error);
            expectGreaterThan (show.requiredSchemaVersion(), 4, "above what the previous Encore accepts");
        }
    }
};

class PedalEqRegistrationTests final : public juce::UnitTest
{
public:
    PedalEqRegistrationTests() : juce::UnitTest ("Pedal EQ registration", "Encore") {}

    void runTest() override
    {
        beginTest ("The Pedal EQ is hidden and a new module");
        {
            const auto* type = HiddenBlockTypeTests::findType (getBlockTypes(), PedalEqBlock::typeId);
            expect (type != nullptr && type->hidden && type->newModule);
            expect (HiddenBlockTypeTests::findType (getMenuBlockTypes(), PedalEqBlock::typeId) == nullptr);

            auto block = createBlock (PedalEqBlock::typeId);
            expect (block != nullptr && block->getTypeId() == PedalEqBlock::typeId);
        }

        beginTest ("A show with a Pedal EQ saves as schema 5, loads back and plays");
        {
            auto show = makeDefaultShow ("G7");
            BlockModel g7;
            g7.id = "g7-1";
            g7.type = PedalEqBlock::typeId;
            g7.params["g800"] = 6.0f;
            g7.params["input"] = 2.0f;
            show.patches[0].mainLane().blocks.insert (show.patches[0].mainLane().blocks.begin(), g7);

            const auto text = json::toJson (show);
            expect (text.contains ("\"schemaVersion\": 5"));

            juce::String error;
            const auto loaded = json::fromJson (text, error);
            expect (loaded.has_value(), error);

            if (! loaded)
                return;

            juce::StringArray problems;
            auto chain = buildChain (loaded->patches[0], ShowFolder(), spec, problems);
            expect (problems.isEmpty(), problems.joinIntoString ("; "));
            const auto& first = chain->getBlocks().front();
            expect (first->getTypeId() == PedalEqBlock::typeId);
            expectEquals (first->findParameter ("g800")->getValue(), 6.0f);
            expectEquals (first->findParameter ("input")->getChoiceIndex(), 2);
        }

        beginTest ("A show round trip keeps the capture; a missing one plays DSP only");
        {
            const auto capture = juce::File (ENCORE_NAM_EXAMPLE_MODELS).getChildFile ("wavenet_a1_standard.nam");
            auto patch = makeDefaultShow ("Capture").patches[0];
            BlockModel g7;
            g7.id = "g7-cap";
            g7.type = PedalEqBlock::typeId;
            g7.file = capture.getFullPathName();
            patch.mainLane().blocks.insert (patch.mainLane().blocks.begin(), g7);

            juce::StringArray problems;
            auto chain = buildChain (patch, ShowFolder(), spec, problems);
            expect (problems.isEmpty(), problems.joinIntoString ("; "));
            auto* eq = dynamic_cast<PedalEqBlock*> (chain->getBlocks().front().get());
            expect (eq != nullptr && eq->getLoadedFile() == capture);

            PatchModel saved = patch;
            captureChain (*chain, saved, ShowFolder());
            expectEquals (saved.mainLane().blocks.front().file, capture.getFullPathName());

            juce::String error;
            const auto back = json::fromJson (json::toJson ([&] { auto show = makeDefaultShow ("Capture"); show.patches[0] = saved; return show; }()), error);
            expect (back.has_value() && back->patches[0].mainLane().blocks.front().file == capture.getFullPathName(), error);

            // Missing: a problem is reported, the block still builds and plays DSP only.
            auto missing = patch;
            missing.mainLane().blocks.front().file = "models/not here.nam";
            problems.clear();
            auto missingChain = buildChain (missing, ShowFolder(), spec, problems);
            auto* fallback = dynamic_cast<PedalEqBlock*> (missingChain->getBlocks().front().get());
            expect (fallback != nullptr && fallback->getLatestCapture() == nullptr);
            expectEquals (fallback->getLoadedFileInfo(), juce::String ("Capture not on this computer: DSP only"));

            PatchModel keep = missing;
            captureChain (*missingChain, keep, ShowFolder());
            expectEquals (keep.mainLane().blocks.front().file, juce::String ("models/not here.nam"), "what was asked for is kept");
            expectEquals (ShowFolder::subfolderFor (PedalEqBlock::typeId), juce::String ("models"), "copied into the show with the amps' captures");
        }
    }
};

static HiddenBlockTypeTests hiddenBlockTypeTests;
static PedalEqRegistrationTests pedalEqRegistrationTests;
static NewModuleSchemaTests newModuleSchemaTests;

} // namespace encore::test
