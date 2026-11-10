// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "TestSignals.h"
#include "FakeDevice.h"

#include <juce_events/juce_events.h>

#include "model/ShowJson.h"
#include "session/ShowSession.h"

namespace encore::test
{

class ShowSessionTests final : public juce::UnitTest
{
public:
    ShowSessionTests() : juce::UnitTest ("Show session", "Encore") {}

    /** Lets background builds finish and their results arrive on the message thread. */
    static void pumpUntil (std::function<bool()> done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            // Like the app: the audio thread takes new tables, the UI timer frees old ones.
            if (pumpedEngine != nullptr)
            {
                playOneBlock (*pumpedEngine);
                pumpedEngine->collectGarbage();
            }
        }
    }

    static inline AudioEngine* pumpedEngine = nullptr;

    /** One audio callback, so the engine picks up its newest chain. */
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
                              .getChildFile ("encore-session-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();
        const auto showFolder = temp.getChildFile ("Gig.encore");

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);
        pumpedEngine = &engine;

        beginTest ("A new show starts with a playable patch");
        {
            ShowSession session (engine);
            expect (session.newShow (showFolder, "Gig").wasOk());
            pumpUntil ([&] { return ! session.isBuilding(); });
            playOneBlock (engine);

            expect (showFolder.getChildFile ("show.json").existsAsFile());
            expectEquals ((int) session.getShow().patches.size(), 1);
            expect (engine.getChain() != nullptr);
            expectEquals ((int) engine.getChain()->getBlocks().size(), 4);
        }

        beginTest ("Knob changes, new patches and block edits are saved and come back");
        {
            juce::String leadId;

            {
                ShowSession session (engine);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);

                // A tweak in the editor goes straight to the live block...
                engine.getChain()->getBlocks()[3]->findParameter ("bass")->setValue (5.0f);
                session.markEdited();

                // ...then a new patch with one block removed and one moved.
                leadId = session.addPatch ("Lead");
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);
                session.removeBlock (2);   // the gate
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);
                session.moveBlock (2, 0);  // EQ to the front
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);

                expect (session.save().wasOk());
            }

            ShowSession reopened (engine);
            expect (reopened.openShow (showFolder).wasOk());
            const auto& show = reopened.getShow();
            expectEquals ((int) show.patches.size(), 2);
            expectEquals (show.patches[0].mainLane().blocks[3].params.at ("bass"), 5.0f, "tweak saved");

            const auto* lead = show.findPatch (leadId);
            expect (lead != nullptr);

            if (lead != nullptr)
            {
                const auto& b = lead->mainLane().blocks;
                expectEquals ((int) b.size(), 3);
                expect (b[0].type == "eq" && b[1].type == "nam" && b[2].type == "cab");
            }

            pumpUntil ([&] { return ! reopened.isBuilding(); });
        }

        beginTest ("Saving keeps a backup, and unsaved edits are saved when the session closes");
        {
            {
                ShowSession session (engine);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); });
                session.renamePatch (session.getActivePatchId(), "Clean (verse)");
                expect (session.hasUnsavedChanges());
            }   // closes without an explicit save

            ShowFolder folder;
            juce::String error;
            const auto saved = ShowFolder::open (showFolder, folder, error);
            expect (saved.has_value() && saved->patches[0].name == "Clean (verse)");
            expectGreaterThan (folder.listBackups().size(), 0);
        }

        beginTest ("A running order steps through patches; edits to it are saved");
        {
            juce::String cleanId, leadId;

            {
                ShowSession session (engine);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);

                cleanId = session.getShow().patches[0].id;
                leadId = session.getShow().patches[1].id;

                session.addCue (cleanId, -1);   // 1: verse
                session.addCue (leadId, 0);     // 2: solo
                session.addCue (cleanId, 1);    // 3: outro
                session.renameCue (1, "Solo");
                expectEquals ((int) session.getCues().size(), 3);

                session.goToCue (0);
                playOneBlock (engine);
                session.syncWithAudio();
                expectEquals (session.getCurrentCue(), 0);
                expect (session.getActivePatchId() == cleanId);

                session.next();
                playOneBlock (engine);
                session.syncWithAudio();
                expect (session.getActivePatchId() == leadId, "next cue plays the lead patch");

                // Move the solo to the end: the position follows the playing cue.
                session.moveCue (1, 1);
                playOneBlock (engine);
                session.syncWithAudio();
                expectEquals (session.getCurrentCue(), 2);
                expect (session.save().wasOk());
            }

            ShowSession reopened (engine);
            expect (reopened.openShow (showFolder).wasOk());
            const auto cues = reopened.getCues();
            expectEquals ((int) cues.size(), 3);

            if (cues.size() == 3)
            {
                expect (cues[2]->name == "Solo" && cues[2]->patchId == leadId);
                expect (cues[0]->patchId == cleanId && cues[1]->patchId == cleanId);
            }

            pumpUntil ([&] { return ! reopened.isBuilding(); });
            playOneBlock (engine);
            reopened.syncWithAudio();
            expectEquals (reopened.getCurrentCue(), 0, "a show with cues starts at the first cue");
        }

        beginTest ("Stomp and expression assignments, and cue MIDI, are saved");
        {
            {
                ShowSession session (engine);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); });
                session.goToCue (0);
                playOneBlock (engine);
                session.syncWithAudio();

                session.setBlockStomp (2, 4);
                expectEquals (engine.getChain()->getBlocks()[2]->getStomp(), 4, "live at once");

                session.setExpression (3, "treble", 2);
                session.setCueProgramChange (0, 12);
                session.setCueMidiOut (0, { { MidiOutMessage::Kind::controlChange, 1, 7, 100 } });
                pumpUntil ([&] { return ! session.isBuilding(); });
                playOneBlock (engine);
                expect (session.save().wasOk());
            }

            ShowSession reopened (engine);
            expect (reopened.openShow (showFolder).wasOk());
            const auto cues = reopened.getCues();
            const auto* patch = reopened.getShow().findPatch (cues[0]->patchId);
            expect (patch != nullptr);

            if (patch != nullptr)
            {
                expectEquals (patch->mainLane().blocks[2].stomp, 4);
                expect (patch->mainLane().blocks[3].expression.count ("treble") == 1);
            }

            expectEquals (cues[0]->programChange, 12);
            expectEquals ((int) cues[0]->midiOut.size(), 1);
            pumpUntil ([&] { return ! reopened.isBuilding(); });
        }

        beginTest ("Stage layouts, per-cue layouts and tuner/panic settings are saved and applied");
        {
            juce::String bigId;

            {
                ShowSession session (engine);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); });

                expectEquals (session.getLayoutForCue (0).name, juce::String ("Default"), "built-in to start");

                bigId = session.addLayout ("Big monitor", {});
                auto widgets = session.getStage().findLayout (bigId)->widgets;
                widgets.push_back ({ StageWidgetType::text, 30, 30, 5, 2, 9.0f, "Watch the MD" });   // off the grid
                session.setLayoutWidgets (bigId, widgets);
                session.setCueLayout (1, bigId);
                session.setTunerMutes (false);
                session.setReferencePitch (442.0f);
                session.setPanicBehaviour (PanicBehaviour::latch);

                expect (! engine.getTunerMutes(), "applied to the engine at once");
                expect (engine.getPanicMode() == AudioEngine::PanicMode::latch);
                expect (session.save().wasOk());
            }

            {
                ShowSession reopened (engine);
                expect (reopened.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! reopened.isBuilding(); });

                expectEquals ((int) reopened.getStage().layouts.size(), 2);
                expectEquals (reopened.getLayoutForCue (0).name, juce::String ("Default"));
                expectEquals (reopened.getLayoutForCue (1).name, juce::String ("Big monitor"));

                const auto& text = reopened.getLayoutForCue (1).widgets.back();
                expectEquals (text.text, juce::String ("Watch the MD"));
                expect (text.x + text.width <= stageGridColumns && text.y + text.height <= stageGridRows, "kept on the grid");
                expectEquals (text.textScale, 3.0f);
                expectEquals (reopened.getStage().referencePitch, 442.0f);
                expectWithinAbsoluteError (engine.getTuner().getReferencePitch(), 442.0f, 0.01f);

                // Deleting a layout sends its cues back to the default; the last one stays.
                expect (reopened.deleteLayout (bigId));
                expectEquals (reopened.getLayoutForCue (1).name, juce::String ("Default"));
                expect (! reopened.deleteLayout (reopened.getStage().layouts.front().id));

                reopened.setTunerMutes (true);
                reopened.setPanicBehaviour (PanicBehaviour::recover);
                expect (reopened.save().wasOk());
            }
        }

        beginTest ("The last patch can't be deleted");
        {
            ShowSession session (engine);
            expect (session.openShow (showFolder).wasOk());
            pumpUntil ([&] { return ! session.isBuilding(); });
            const auto first = session.getShow().patches[0].id;
            const auto second = session.getShow().patches[1].id;
            expect (session.deletePatch (first));
            expect (! session.deletePatch (second));
            expect (session.getActivePatchId() == second);
            pumpUntil ([&] { return ! session.isBuilding(); });
        }

        pumpedEngine = nullptr;
        engine.audioDeviceStopped();
        engine.collectGarbage();
        temp.deleteRecursively();
    }
};

static ShowSessionTests showSessionTests;

} // namespace encore::test
