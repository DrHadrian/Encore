// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2: the gates in a show session. Removing a leader
// unlinks its followers in the same edit.

#include "TestSignals.h"
#include "FakeDevice.h"

#include <juce_events/juce_events.h>

#include "engine/blocks/PedalGateBlock.h"
#include "engine/blocks/StudioGateBlock.h"
#include "model/ShowJson.h"
#include "session/ShowSession.h"

namespace encore::test
{

class GateSessionTests final : public juce::UnitTest
{
public:
    GateSessionTests() : juce::UnitTest ("Gate session", "Encore") {}

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

    static BlockModel gateModel (const char* id, const char* type, std::map<juce::String, juce::String> refs = {})
    {
        BlockModel b;
        b.id = id;
        b.type = type;
        b.refs = std::move (refs);
        return b;
    }

    static juce::File writeShow (const juce::File& folder, const ShowModel& show)
    {
        folder.createDirectory();
        folder.getChildFile ("show.json").replaceWithText (json::toJson (show));
        return folder;
    }

    void runTest() override
    {
        juce::MessageManager::getInstance();
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-gates-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);

        beginTest ("Removing a leader unlinks its followers, and drops keys from it, in the same edit");
        {
            auto show = makeDefaultShow ("Links");
            auto& blocks = show.patches[0].mainLane().blocks;
            blocks.insert (blocks.begin(), gateModel ("lead", PedalGateBlock::typeId));
            blocks.push_back (gateModel ("follow", StudioGateBlock::typeId, { { "linkTo", "lead" }, { "keyBlock", "lead" }, { "keyInput", "Input 2" } }));
            blocks.push_back (gateModel ("other", PedalGateBlock::typeId, { { "linkTo", "follow" } }));

            ShowSession session (engine);
            expect (session.openShow (writeShow (temp.getChildFile ("Links.encore"), show)).wasOk());
            pumpUntil (engine, [&] { return ! session.isBuilding(); });

            auto* chain = engine.getChain();
            expect (chain != nullptr);

            if (chain != nullptr)
            {
                auto* follower = dynamic_cast<GateBlock*> (chain->getBlocks()[(size_t) chain->indexOfModelId ("follow")].get());
                expect (follower != nullptr && follower->getLinkState() == GateBlock::LinkState::following);
            }

            session.removeBlock (0);   // the leader
            pumpUntil (engine, [&] { return ! session.isBuilding(); });

            const auto* patch = session.getPatch (session.getActivePatchId());
            expect (patch != nullptr);

            if (patch != nullptr)
            {
                const auto& after = patch->mainLane().blocks;
                const auto& follow = *std::find_if (after.begin(), after.end(), [] (auto& b) { return b.id == "follow"; });
                expect (follow.refs.count ("linkTo") == 0 && follow.refs.count ("keyBlock") == 0, "unlinked, its key dropped");
                expectEquals (follow.refs.at ("keyInput"), juce::String ("Input 2"), "an input isn't a block: kept");
                const auto& other = *std::find_if (after.begin(), after.end(), [] (auto& b) { return b.id == "other"; });
                expectEquals (other.refs.at ("linkTo"), juce::String ("follow"), "a link to a block that stays is kept");
            }

            playOneBlock (engine);

            if (auto* rebuilt = engine.getChain())
            {
                auto* follower = dynamic_cast<GateBlock*> (rebuilt->getBlocks()[(size_t) rebuilt->indexOfModelId ("follow")].get());
                expect (follower != nullptr && follower->getLinkState() == GateBlock::LinkState::none);
            }
        }

        temp.deleteRecursively();
    }
};

static GateSessionTests gateSessionTests;

} // namespace encore::test
