// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B0 (decision 0062): hidden block types, and the show
// schema of shows that use a new module.

#include "TestSignals.h"

#include "engine/blocks/EngineProbeBlock.h"
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

static HiddenBlockTypeTests hiddenBlockTypeTests;

} // namespace encore::test
