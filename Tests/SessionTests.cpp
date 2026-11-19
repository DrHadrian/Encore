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

        beginTest ("Theatre cue lists: sections, insert, move, cut and automatic numbering");
        {
            ShowSession session (engine);
            expect (session.newShow (temp.getChildFile ("Theatre.encore"), "Theatre").wasOk());
            pumpUntil ([&] { return ! session.isBuilding(); });

            expectEquals (session.getWords().list, juce::String ("Set list"), "a new show is a band's set list");
            session.setShowTemplate (ShowTemplate::musical);
            expectEquals (session.getWords().list, juce::String ("Cue list"));
            expectEquals (session.getWords().item, juce::String ("cue"));

            const auto act1 = session.insertSection ({}, "Act 1", "act");
            const auto overture = session.insertCue (act1);          // first in Act 1
            session.setItemName (overture, "Overture");
            const auto think = session.insertCue (overture);
            session.setItemName (think, "Think of me");
            const auto act2 = session.insertSection (act1, "Act 2", "act");
            const auto masq = session.insertCue (act2);
            session.setItemName (masq, "Masquerade");
            session.setItemBarMark (masq, "bar 12 / B");
            session.setItemNote (masq, "watch conductor");

            auto names = [&]
            {
                juce::StringArray out;

                for (auto* cue : session.getCues())
                    out.add (cue->number + " " + cue->name);

                return out.joinIntoString (", ");
            };

            expectEquals (names(), juce::String ("1 Overture, 2 Think of me, 3 Masquerade"));
            expectEquals (session.getSectionPath (2), juce::String ("Act 2"));
            expectEquals (session.findCueByNumber ("2"), 1);

            // Insert between: everything after renumbers.
            const auto angel = session.insertCue (overture);
            session.setItemName (angel, "Angel of Music");
            expectEquals (names(), juce::String ("1 Overture, 2 Angel of Music, 3 Think of me, 4 Masquerade"));

            // Cut: skipped and unnumbered, but kept.
            session.setCut (angel, true);
            expectEquals (names(), juce::String ("1 Overture, 2 Think of me, 3 Masquerade"));
            expectEquals ((int) session.getRunningOrderRows().size(), 6, "2 sections + 4 cues, cut included");
            session.setCut (angel, false);
            expectEquals (names(), juce::String ("1 Overture, 2 Angel of Music, 3 Think of me, 4 Masquerade"));

            // Moving down out of Act 1 lands at the start of Act 2 ("into the neighbouring section").
            session.moveItem (think, +1);   // last in Act 1 -> out, after Act 1 (top level)
            session.moveItem (think, +1);   // next to Act 2 -> into it, first
            expectEquals (session.getSectionPath (session.getCueIndexOf (think)), juce::String ("Act 2"));
            expectEquals (names(), juce::String ("1 Overture, 2 Angel of Music, 3 Think of me, 4 Masquerade"));
            session.moveItem (think, +1);   // after Masquerade
            expectEquals (names(), juce::String ("1 Overture, 2 Angel of Music, 3 Masquerade, 4 Think of me"));

            // A new cue plays the patch of the cue before it.
            const auto lead = session.addPatch ("Lead");
            session.setItemPatch (masq, lead);
            const auto after = session.insertCue (masq);
            expectEquals (session.getCues()[(size_t) session.getCueIndexOf (after)]->patchId, lead);

            // Deleting a section keeps its cues.
            session.deleteItem (act2);
            expectEquals ((int) session.getCues().size(), 5);
            expectEquals (session.getSectionPath (2), juce::String());

            // The playing cue stays the playing cue through edits above it.
            session.goToCue (3);
            playOneBlock (engine);
            session.syncWithAudio();
            const auto playingId = session.getCues()[3]->id;
            session.insertCue (overture);
            playOneBlock (engine);
            session.syncWithAudio();
            expectEquals (session.getCues()[(size_t) session.getCurrentCue()]->id, playingId);

            // Numbers and sections survive saving.
            expect (session.save().wasOk());
            ShowSession reopened (engine);
            expect (reopened.openShow (temp.getChildFile ("Theatre.encore")).wasOk());
            pumpUntil ([&] { return ! reopened.isBuilding(); });
            expectEquals (reopened.getCues()[2]->number, juce::String ("3"));
            expect (reopened.getShow().showTemplate == ShowTemplate::musical, "the kind of show is saved");
            expectEquals (reopened.getCues()[(size_t) reopened.getCueIndexOf (masq)]->note, juce::String ("watch conductor"));
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
