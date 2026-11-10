// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/core/Realtime.h"
#include "session/PatchBuilder.h"

namespace encore::test
{

class PatchBuilderTests final : public juce::UnitTest
{
public:
    PatchBuilderTests() : juce::UnitTest ("Patches to chains and back", "Encore") {}

    static void writeIr (const juce::File& file)
    {
        juce::AudioBuffer<float> b (1, 256);
        b.clear();
        b.setSample (0, 0, 1.0f);
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat wav;

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                                                     .withNumChannels (1)
                                                                                     .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (b, 0, 256);
    }

    void runTest() override
    {
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-builder-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        const auto showFolder = temp.getChildFile ("Show.encore");
        showFolder.getChildFile ("irs").createDirectory();
        const auto ir = showFolder.getChildFile ("irs/cab.wav");
        writeIr (ir);
        const ShowFolder folder (showFolder);

        const auto ampFile = juce::File (ENCORE_NAM_EXAMPLE_MODELS).getChildFile ("wavenet_a1_standard.nam");

        auto patch = makeDefaultPatch ("Lead");
        auto& blocks = patch.mainLane().blocks;
        blocks[0].file = ampFile.getFullPathName();       // outside the show (not collected yet)
        blocks[0].params["input"] = 7.5f;
        blocks[1].file = "irs/cab.wav";                    // inside the show
        blocks[2].enabled = false;
        blocks[3].params["mid"] = -3.0f;

        beginTest ("A built chain matches its patch and is ready to play at once");
        {
            juce::StringArray problems;
            auto chain = buildChain (patch, folder, spec, problems);
            expect (problems.isEmpty(), problems.joinIntoString ("; "));

            const auto& built = chain->getBlocks();
            expectEquals ((int) built.size(), 4);
            expect (built[0]->getTypeId() == "nam" && built[1]->getTypeId() == "cab"
                    && built[2]->getTypeId() == "gate" && built[3]->getTypeId() == "eq");
            expectEquals (built[0]->findParameter ("input")->getValue(), 7.5f);
            expect (! built[2]->isEnabled());
            expect (built[0]->getLoadedFile() == ampFile);
            expect (built[1]->getLoadedFile() == ir);
            expect (chain->getSpec() == spec, "prepared");

            // First block after the handover already goes through the amp (no fade-in from dry),
            // and nothing allocates.
            auto signal = sine (110.0, 0.3f, blockSize);
            const auto dry = signal;
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;
                float* ch[1] = { signal.data() };
                chain->process (ch, 1, blockSize);
            }
            expectEquals (realtimeAllocationCount() - before, 0);
            expect (signal != dry, "the amp is active from the first block");
        }

        beginTest ("Capturing a chain gives back the same patch, with live tweaks");
        {
            juce::StringArray problems;
            auto chain = buildChain (patch, folder, spec, problems);
            chain->getBlocks()[3]->findParameter ("treble")->setValue (4.0f);   // a tweak in the editor

            auto captured = patch;
            captureChain (*chain, captured, folder);
            const auto& c = captured.mainLane().blocks;

            expectEquals ((int) c.size(), 4);

            for (size_t i = 0; i < 4; ++i)
            {
                expect (c[i].id == blocks[i].id, "IDs are kept");
                expect (c[i].type == blocks[i].type);
                expect (c[i].enabled == blocks[i].enabled);
            }

            expectEquals (c[0].params.at ("input"), 7.5f);
            expectEquals (c[3].params.at ("mid"), -3.0f);
            expectEquals (c[3].params.at ("treble"), 4.0f);
            expect (c[0].file == ampFile.getFullPathName(), "outside files stay absolute until saved");
            expect (c[1].file == "irs/cab.wav", "files in the show stay relative");
        }

        beginTest ("Stomps and expression pedals are built, and a pedal never overwrites the saved knob");
        {
            auto withPedals = patch;
            withPedals.mainLane().blocks[3].stomp = 2;
            withPedals.mainLane().blocks[3].expression["bass"] = { 1, -12.0f, 12.0f };
            withPedals.mainLane().blocks[3].params["bass"] = 3.0f;   // the saved knob value

            juce::StringArray problems;
            auto chain = buildChain (withPedals, folder, spec, problems);
            expectEquals (chain->getBlocks()[3]->getStomp(), 2);
            expect (chain->usesExpression (1));

            chain->applyExpression (1, 1.0f);   // toe down: bass to +12
            expectEquals (chain->getBlocks()[3]->findParameter ("bass")->getValue(), 12.0f);
            expect (chain->toggleStomp (2));
            expect (! chain->getBlocks()[3]->isEnabled());

            auto captured = withPedals;
            captureChain (*chain, captured, folder);
            const auto& eq = captured.mainLane().blocks[3];
            expectEquals (eq.params.at ("bass"), 3.0f, "the pedal position isn't saved as the knob");
            expectEquals (eq.stomp, 2);
            expect (! eq.enabled, "the stomp state is saved");
            expect (eq.expression.count ("bass") == 1, "the assignment survives capture");
        }

        beginTest ("A missing file or unknown block is reported, nothing is lost");
        {
            auto broken = patch;
            broken.mainLane().blocks[0].file = "models/not-there.nam";
            BlockModel future;
            future.id = newId();
            future.type = "laser-harp";
            broken.mainLane().blocks.push_back (future);

            juce::StringArray problems;
            auto chain = buildChain (broken, folder, spec, problems);
            expectEquals (problems.size(), 2);
            expectEquals ((int) chain->getBlocks().size(), 4, "unknown type skipped");

            auto captured = broken;
            captureChain (*chain, captured, folder);
            expect (captured.mainLane().blocks[0].file == "models/not-there.nam", "the path survives");
        }

        temp.deleteRecursively();
    }
};

static PatchBuilderTests patchBuilderTests;

} // namespace encore::test
