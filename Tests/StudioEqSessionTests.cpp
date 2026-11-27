// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B3: a Studio EQ's bands edited in a running show.

#include "TestSignals.h"
#include "FakeDevice.h"

#include <juce_events/juce_events.h>

#include "engine/blocks/StudioEqBlock.h"
#include "model/ShowJson.h"
#include "session/ShowSession.h"

namespace encore::test
{

class StudioEqSessionTests final : public juce::UnitTest
{
public:
    StudioEqSessionTests() : juce::UnitTest ("Studio EQ session", "Encore") {}

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

    void runTest() override
    {
        juce::MessageManager::getInstance();
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-q24-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);

        beginTest ("Adding a band is heard at once (no rebuild); removing one with an assignment drops it");
        {
            auto show = makeDefaultShow ("Q24");
            BlockModel q24;
            q24.id = "q24";
            q24.type = StudioEqBlock::typeId;
            auto& blocks = show.patches[0].mainLane().blocks;
            blocks.insert (blocks.begin(), q24);
            const auto folder = temp.getChildFile ("Q24.encore");
            folder.createDirectory();
            folder.getChildFile ("show.json").replaceWithText (json::toJson (show));

            ShowSession session (engine);
            expect (session.openShow (folder).wasOk());
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            auto* chainBefore = engine.getChain();

            StudioEqBlock::BandSettings band;
            band.shape = StudioEqBlock::Shape::bell;
            band.frequency = 700.0;
            band.gainDb = 5.0;
            juce::String error;
            expectEquals (session.addEqBand (0, band, error), 0);
            expect (! session.isBuilding(), "no rebuild for a new band");
            playOneBlock (engine);
            expect (engine.getChain() == chainBefore);

            auto* live = dynamic_cast<StudioEqBlock*> (engine.getChain()->getBlocks().front().get());
            expect (live != nullptr && live->getNumBandsInUse() == 1 && live->getBand (0).frequency == 700.0);

            session.setExpression (0, "b01Gain", 1);
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            playOneBlock (engine);
            expect (engine.getChain()->usesExpression (1));

            session.setExpression (0, "b01Used", 2);   // not a sound to sweep: refused
            expectEquals (session.getExpressionPedal (0, "b01Used"), 0);

            session.removeEqBand (0, 0);
            pumpUntil (engine, [&] { return ! session.isBuilding(); });
            playOneBlock (engine);
            expectEquals (session.getExpressionPedal (0, "b01Gain"), 0, "the assignment went with the band");
            expect (! engine.getChain()->usesExpression (1));
            live = dynamic_cast<StudioEqBlock*> (engine.getChain()->getBlocks().front().get());
            expect (live != nullptr && live->getNumBandsInUse() == 0);
        }

        temp.deleteRecursively();
    }
};

static StudioEqSessionTests studioEqSessionTests;

} // namespace encore::test
