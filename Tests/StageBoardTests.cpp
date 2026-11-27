// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The stage view rendered off-screen with the Stage boards' shows and layout
// (docs/design/reference/target/stage*.png): snapshots for ui-compare.py,
// the lost and go-to strips, and the window sizes the stage must survive.

#include "BoardFixtures.h"
#include "FakeDevice.h"
#include "TestSignals.h"
#include "UiSnapshot.h"

#include "ui/StageView.h"
#include "ui/kit/MuteButton.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class StageBoardTests final : public juce::UnitTest
{
public:
    StageBoardTests() : juce::UnitTest ("Stage view (design v3)", "Encore") {}

    /** One audio callback, then the session catches up with the audio thread. */
    static void play (AudioEngine& engine, ShowSession& session)
    {
        std::vector<float> in ((size_t) blockSize, 0.0f), outL ((size_t) blockSize), outR ((size_t) blockSize);
        const float* inputs[] = { in.data() };
        float* outputs[] = { outL.data(), outR.data() };
        engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, blockSize, {});
        engine.collectGarbage();
        session.syncWithAudio();
    }

    static void settle (AudioEngine& engine, ShowSession& session)
    {
        for (int i = 0; i < 300 && session.isBuilding(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        for (int i = 0; i < 4; ++i)
            play (engine, session);
    }

    /** The board's status: ASIO at 64 samples, 7 % CPU, levels at 62 and 46 %. */
    static StageSnapshot boardSnapshot()
    {
        StageSnapshot s;
        s.cpu = 0.07;
        s.overruns = 0;
        s.roundTripMs = 5.3;
        s.deviceLine = juce::String (juce::CharPointer_UTF8 ("ASIO \xc2\xb7 48 kHz \xc2\xb7 64 samples \xc2\xb7 5.3 ms round trip"));
        s.footswitch = "Footswitch connected";
        s.inputPeak = juce::Decibels::decibelsToGain (-60.0f + 0.62f * 60.0f);
        s.outputPeak = juce::Decibels::decibelsToGain (-60.0f + 0.46f * 60.0f);
        return s;
    }

    /** The board's layout as the show's default, expression pedal 1 at 72 %. */
    static void setUpStage (AudioEngine& engine, ShowSession& session, const std::vector<StageWidget>& widgets = boardStageWidgets())
    {
        const auto id = session.addLayout ("Default", {});
        session.setLayoutWidgets (id, widgets);
        session.setDefaultLayout (id);

        MidiBinding pedal;
        pedal.action = MidiAction::expression;
        pedal.index = 1;
        pedal.trigger = { MidiTrigger::Kind::controlChange, 0, 11 };
        engine.getMidiRouter().setBindings ({ pedal });
        engine.getMidiRouter().receive (0xb0, 11, (juce::uint8) juce::roundToInt (0.72f * 127.0f));
        engine.setMuted (false);
    }

    void runTest() override
    {
        beginTest ("Set-up: the board's show, playing cue 3");

        ScopedEncoreLook look;
        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);

        {
            ShowSession session (engine);
            expect (session.openShow (makeStageShow (root)).wasOk());
            setUpStage (engine, session);
            settle (engine, session);
            session.goToCue (2);   // "Ballad in D"
            settle (engine, session);
            expectEquals (session.getCurrentCue(), 2);

            StageView view (engine, session);
            view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);

            beginTest ("Stage: the board's show at the reference size, 1x and 2x (stage.png)");
            {
                for (int i = 0; i < 3; ++i)
                    view.update (boardSnapshot());

                expect (hasContent (renderSnapshot (view, "stage")));
                renderSnapshot (view, "stage@2x", 2.0f);
            }

            beginTest ("Stage at the BStage sizes (v32-stage, v32-stage-outline): S2 tap again, S3 hold for the Tuner, S7 lit stomps");
            {
                // The live Stage has no name tags; Patch, Next cue and Cue note carry headings.
                expect (! view.isEditingLayout());

                // S2: a tap on another song arms it; a tap on the playing one does nothing.
                view.tapCue (2);
                expectEquals (view.getArmedCue(), -1, "the cue playing: nothing to arm");
                view.tapCue (5);   // "6 Acoustic Set"
                expectEquals (view.getArmedCue(), 5, "a first tap arms it");
                expectEquals (session.getCurrentCue(), 2, "and doesn't go");

                for (int i = 0; i < 3; ++i)
                    view.update (boardSnapshot());

                renderSnapshot (view, "v32-stage");
                renderSnapshot (view, "v32-stage@2x", 2.0f);

                // Next and Panic at "Outline".
                {
                    auto widgets = view.getShownWidgets();

                    for (auto& w : widgets)
                        if (w.type == StageWidgetType::nextButton || w.type == StageWidgetType::panicButton)
                            w.outlineAtRest = true;

                    session.setLayoutWidgets (session.getShow().stage.defaultLayoutId, widgets);
                    settle (engine, session);

                    for (int i = 0; i < 3; ++i)
                        view.update (boardSnapshot());

                    renderSnapshot (view, "v32-stage-outline");
                    renderSnapshot (view, "v32-stage-outline@2x", 2.0f);

                    for (auto& w : widgets)
                        w.outlineAtRest = false;

                    session.setLayoutWidgets (session.getShow().stage.defaultLayoutId, widgets);
                    settle (engine, session);
                }

                // The second tap (within 2 s) goes; the armed row clears. (Armed again first:
                // the snapshots above take about the 2 s in Debug, so the first tap may have lapsed.)
                view.tapCue (session.getCurrentCue());   // a tap on the playing cue clears any armed one
                expectEquals (view.getArmedCue(), -1);
                view.tapCue (5);
                expectEquals (view.getArmedCue(), 5);
                view.tapCue (5);
                expectEquals (view.getArmedCue(), -1, "the second tap clears the armed row");
                settle (engine, session);   // going to a cue builds its patch first
                expectEquals (session.getCurrentCue(), 5, "a second tap goes");

                // An armed tap that isn't followed lapses after 2 s.
                view.tapCue (3);
                expectEquals (view.getArmedCue(), 3);
                juce::Thread::sleep (2100);
                view.update (boardSnapshot());
                expectEquals (view.getArmedCue(), -1, "back to normal after 2 s");

                // S3: Tuner opens only when held, and a tap closes it.
                expect (! engine.isTunerOpen());
                view.beginTunerHold();
                view.endTunerHold();
                view.update (boardSnapshot());
                expect (! engine.isTunerOpen(), "a brush doesn't open the tuner");
                view.beginTunerHold();
                juce::Thread::sleep (550);
                view.update (boardSnapshot());
                expect (engine.isTunerOpen(), "held 0.5 s: it opens");
                view.endTunerHold();
                engine.setTunerOpen (false);
                view.update (boardSnapshot());

                session.goToCue (2);
                settle (engine, session);

                for (int i = 0; i < 3; ++i)
                    view.update (boardSnapshot());
            }

            beginTest ("Stage: the interface lost (stage-interface-lost.png)");
            {
                auto lost = boardSnapshot();
                lost.audioProblem = juce::String (juce::CharPointer_UTF8 ("AUDIO INTERFACE LOST - reconnecting to Focusrite USB ASIO\xe2\x80\xa6"));
                lost.deviceLine = lost.audioProblem + " (staying on the cue; check the cable)";
                lost.roundTripMs = 0.0;

                for (int i = 0; i < 3; ++i)
                    view.update (lost);

                expect (hasContent (renderSnapshot (view, "stage-interface-lost")));
                renderSnapshot (view, "stage-interface-lost@2x", 2.0f);

                view.update (boardSnapshot());   // back: the strip goes, the grid grows again
            }

            beginTest ("Stage: muted, locked and the tuner");
            {
                engine.setMuted (true);
                view.update (boardSnapshot());
                expect (hasContent (renderSnapshot (view, "stage-muted")));
                engine.setMuted (false);

                view.setLocked (true);
                view.update (boardSnapshot());
                expect (view.isLocked());
                expect (hasContent (renderSnapshot (view, "stage-locked-board")));
                view.setLocked (false);

                engine.setTunerOpen (true);
                auto tuning = boardSnapshot();
                tuning.tuner = { true, 109.8f, 45, -2.0f, 0.1f };
                view.update (tuning);
                expect (hasContent (renderSnapshot (view, "stage-tuner-overlay")));
                engine.setTunerOpen (false);
                view.update (boardSnapshot());
            }

            beginTest ("Stage: small, huge and tiny windows never break");
            {
                for (auto size : { juce::Point<int> (1024, 640), juce::Point<int> (3840, 2160), juce::Point<int> (640, 480),
                                   juce::Point<int> (1, 1), juce::Point<int> (0, 0) })
                {
                    view.setSize (size.x, size.y);
                    view.update (boardSnapshot());

                    if (size.x >= 640)
                        expect (hasContent (renderSnapshot (view, "stage-" + juce::String (size.x) + "x" + juce::String (size.y))));
                }

                view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            }
        }

        beginTest ("Stage with the Outputs widget: a meter and Mute / MUTED per output, Amp DI muted (m11-stage-outputs)");
        {
            ShowSession session (engine);
            expect (session.openShow (makeStageShow (root)).wasOk());
            setUpStage (engine, session, outputsStageWidgets());
            engine.setRouting (makeBoardRouting());
            settle (engine, session);
            session.goToCue (2);   // "Ballad in D"
            settle (engine, session);

            StageView view (engine, session);
            view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            juce::StringArray muteCalls;
            view.onMuteOutput = [&] (const juce::String& name, bool muted)
            {
                muteCalls.add (name + (muted ? " muted" : " live"));
                engine.setOutputMuted (engine.getOutputSlot (name), muted);
            };

            // The board's levels: front of house at about 60 %, in-ear 45 %, Amp DI muted.
            auto levels = boardSnapshot();
            const auto gain = [] (float share) { return juce::Decibels::decibelsToGain (-60.0f + share * 60.0f); };
            levels.outputSlotPeaks[(size_t) engine.getOutputSlot ("Front of house")] = { gain (0.61f), gain (0.54f) };
            levels.outputSlotPeaks[(size_t) engine.getOutputSlot ("In-ear")] = { gain (0.46f), gain (0.39f) };

            for (int i = 0; i < 3; ++i)
                view.update (levels);

            expect (hasContent (renderSnapshot (view, "m11-stage-outputs")));
            renderSnapshot (view, "m11-stage-outputs@2x", 2.0f);

            std::vector<kit::MuteButton*> mutes;
            std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* m = dynamic_cast<kit::MuteButton*> (child))
                        mutes.push_back (m);

                    collect (*child);
                }
            };
            collect (view);
            expectEquals ((int) mutes.size(), 3);

            if (mutes.size() == 3)
            {
                expect (! mutes[0]->getToggleState() && ! mutes[1]->getToggleState() && mutes[2]->getToggleState(), "Amp DI MUTED");

                mutes[1]->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                expect (muteCalls.contains ("In-ear muted"), muteCalls.joinIntoString (", "));
                expect (engine.isOutputMuted (engine.getOutputSlot ("In-ear")));

                view.setLocked (true);
                muteCalls.clear();
                mutes[2]->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                expect (muteCalls.isEmpty(), "locked: Mute does nothing");
                expect (mutes[2]->getToggleState(), "and the button stays as it was");
                view.setLocked (false);
            }

            engine.setRouting (makeDefaultRouting());
        }

        beginTest ("Layout editor: the current cue selected, inspector on (layout-editor.png)");
        {
            ShowSession session (engine);
            expect (session.openShow (makeLayoutShow (root)).wasOk());
            const auto id = session.addLayout ("Default", {});
            session.setLayoutWidgets (id, layoutEditorWidgets());
            session.setDefaultLayout (id);
            session.addLayout ("Big monitor", id);
            settle (engine, session);
            session.goToCue (0);
            settle (engine, session);

            StageView view (engine, session);
            view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            view.setEditingLayout (true);
            expect (view.isEditingLayout());
            view.selectLayoutWidget (0);   // Current cue
            view.update (boardSnapshot());

            expect (hasContent (renderSnapshot (view, "layout-editor")));
            renderSnapshot (view, "layout-editor@2x", 2.0f);

            // "At rest" for Next button and Panic button (decided 19 Nov): Full colour (default) | Outline,
            // in the inspector and the right-click menu, stored per widget with the show.
            {
                const auto& shown = view.getShownWidgets();
                const auto indexOf = [&shown] (StageWidgetType t)
                {
                    for (size_t i = 0; i < shown.size(); ++i)
                        if (shown[i].type == t)
                            return (int) i;
                    return -1;
                };
                const auto next = indexOf (StageWidgetType::nextButton);
                expect (next >= 0, "the board's layout has a Next button");
                auto* atRest = view.getLayoutAtRest();
                expect (atRest != nullptr);

                view.selectLayoutWidget (0);   // Current cue: no "At rest"
                expect (atRest != nullptr && ! atRest->isVisible(), "only Next button and Panic button have it");

                if (next >= 0 && atRest != nullptr)
                {
                    view.selectLayoutWidget (next);
                    expect (atRest->isVisible());
                    expectEquals (atRest->getSelected(), 0, "Full colour by default");
                    expectEquals (atRest->getSegmentButton (0)->getButtonText(), juce::String ("Full colour"));
                    expectEquals (atRest->getSegmentButton (1)->getButtonText(), juce::String ("Outline"));
                    expectEquals (atRest->getSegmentButton (1)->getTooltip(),
                                  juce::String ("Dark with a coloured outline until it's pressed, so the cue and any warning stand out in a dark room"));
                    atRest->getSegmentButton (1)->onClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // the commit is async
                    settle (engine, session);
                    view.update (boardSnapshot());
                    expect (view.getShownWidgets()[(size_t) next].outlineAtRest, "stored with the widget");
                    expectEquals (atRest->getSelected(), 1);
                    renderSnapshot (view, "v32-layout-at-rest");
                    renderSnapshot (view, "v32-layout-at-rest@2x", 2.0f);

                    // Saved with the show: it survives the round trip; Full colour isn't written.
                    StageSettings stage;
                    StageLayout layout;
                    layout.id = "x";
                    layout.widgets = view.getShownWidgets();
                    stage.layouts = { layout };
                    const auto back = stageFromVar (stageToVar (stage));
                    expect (back.layouts.size() == 1 && back.layouts[0].widgets == layout.widgets, "At rest round-trips");

                    atRest->getSegmentButton (0)->onClick();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // the commit is async
                    settle (engine, session);
                    view.update (boardSnapshot());
                    expect (! view.getShownWidgets()[(size_t) next].outlineAtRest);
                }

                view.selectLayoutWidget (0);
            }

            // Inspector off: the artboard takes the width; nothing selected: the same.
            view.setLayoutInspectorShown (false);
            expect (! view.isLayoutInspectorShown());
            expect (hasContent (renderSnapshot (view, "layout-editor-no-inspector")));
            view.setLayoutInspectorShown (true);
            view.selectLayoutWidget (-1);
            view.update (boardSnapshot());

            for (auto size : { juce::Point<int> (640, 480), juce::Point<int> (3840, 2160), juce::Point<int> (1, 1) })
            {
                view.setSize (size.x, size.y);
                view.update (boardSnapshot());
            }

            view.setEditingLayout (false);
            expect (! view.isEditingLayout());
        }

        beginTest ("Layout editor: drag from the list, move on the stage, nearest free space, refuse when full (V3.2-CHANGES 7)");
        {
            using T = StageWidgetType;
            const auto widget = [] (T t, int x, int y, int w, int h) { StageWidget sw; sw.type = t; sw.x = x; sw.y = y; sw.width = w; sw.height = h; return sw; };

            ShowSession session (engine);
            expect (session.openShow (makeLayoutShow (root)).wasOk());
            const auto id = session.addLayout ("Default", {});
            session.setLayoutWidgets (id, { widget (T::currentCue, 0, 0, 16, 6), widget (T::clock, 16, 0, 4, 2) });
            session.setDefaultLayout (id);
            settle (engine, session);
            session.goToCue (0);
            settle (engine, session);

            StageView view (engine, session);
            view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            view.setEditingLayout (true);
            view.update (boardSnapshot());

            auto* list = view.getLayoutWidgetList();
            auto* surface = view.getLayoutSurface();
            expect (list != nullptr && surface != nullptr);

            if (list == nullptr || surface == nullptr)
                return;

            // A point on the stage in cells (the surface's size changes as the inspector comes and goes).
            const auto at = [&] (float cx, float cy)
            {
                return juce::Point<int> (juce::roundToInt (cx * (float) surface->getWidth() / (float) stageGridColumns),
                                         juce::roundToInt (cy * (float) surface->getHeight() / (float) stageGridRows));
            };
            const auto now = juce::Time::getCurrentTime();

            // A mouse event on c at p (c's coordinates), pressed at `down`.
            const auto event = [now] (juce::Component& c, juce::Point<int> p, juce::Point<int> down, bool dragged)
            {
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), juce::ModifierKeys::leftButtonModifier,
                                         juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                         juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                         juce::MouseInputSource::defaultTiltY, &c, &c, now, down.toFloat(), now, 1, dragged);
            };

            // Drag a list row to a point on the stage (surface coordinates); `release` lets go there.
            const auto dragFromList = [&] (T type, juce::Point<int> onStage, bool release)
            {
                const auto row = view.getLayoutListRow (type);
                const auto p = list->getLocalPoint (surface, onStage);
                list->mouseDown (event (*list, row, row, false));
                list->mouseDrag (event (*list, p, row, true));

                if (release)
                    list->mouseUp (event (*list, p, row, true));
            };

            const auto overlaps = [] (const std::vector<StageWidget>& ws)
            {
                for (size_t i = 0; i < ws.size(); ++i)
                    for (size_t j = i + 1; j < ws.size(); ++j)
                        if (ws[i].x < ws[j].x + ws[j].width && ws[j].x < ws[i].x + ws[i].width
                            && ws[i].y < ws[j].y + ws[j].height && ws[j].y < ws[i].y + ws[i].height)
                            return true;

                return false;
            };

            // The list: a grip on each row, the tooltip says what a drag and a click do.
            if (auto* tip = dynamic_cast<juce::TooltipClient*> (list))
            {
                list->mouseMove (event (*list, view.getLayoutListRow (T::patchName), view.getLayoutListRow (T::patchName), false));
                expectEquals (tip->getTooltip(), juce::String ("Drag onto the stage, or click to add it at the first free place"));
            }

            // 1. Drag "Patch" over free cells: the drop box shows "Drop: Patch", 4 x 2, snapped.
            dragFromList (T::patchName, at (6.0f, 9.0f), false);
            expect (view.getLayoutDropBox() != nullptr && view.getLayoutDropBox()->isVisible(), "the drop box shows while dragging");
            expectEquals (view.getLayoutDropText(), juce::String ("Drop: Patch"));
            expect (! view.isLayoutDropRefused());
            expect (! view.getLayoutDragImage().isVisible(), "over the stage the drop box stands for the dragged row");
            renderSnapshot (view, "v32-layout-drop");

            list->mouseUp (event (*list, list->getLocalPoint (surface, at (6.0f, 9.0f)), view.getLayoutListRow (T::patchName), true));
            settle (engine, session);
            expect (! view.getLayoutDropBox()->isVisible(), "gone after the drop");
            expect (! view.getLayoutDragImage().isVisible(), "the row's picture goes with the drop");
            {
                const auto& ws = view.getShownWidgets();
                expectEquals ((int) ws.size(), 3);
                const auto& added = ws.back();
                expect (added.type == T::patchName);
                expectEquals (added.width, 4);
                expectEquals (added.height, 2);
                expectEquals (added.x, 4);   // centred on the pointer, snapped
                expectEquals (added.y, 8);
                expectEquals (view.getSelectedLayoutWidget(), 2, "the dropped widget is selected");
            }

            // 2. Dropped onto taken cells: the nearest free space of its size.
            dragFromList (T::setList, at (4.0f, 2.0f), true);
            settle (engine, session);
            expectEquals ((int) view.getShownWidgets().size(), 4);
            expect (! overlaps (view.getShownWidgets()), "nothing ends up on top of another widget");
            expectEquals (view.getShownWidgets().back().y, 6, "the nearest free row under the current cue");

            // 3. Move the clock by dragging it: the original goes faint, "Move: Clock", it keeps the grab point.
            {
                const auto clockIndex = 1;
                const auto grabPoint = at (17.0f, 1.0f);   // a cell into the clock
                const auto to = at (21.0f, 13.0f);
                surface->mouseDown (event (*surface, grabPoint, grabPoint, false));
                surface->mouseDrag (event (*surface, to, grabPoint, true));
                expectEquals (view.getLayoutDropText(), juce::String ("Move: Clock"));
                expectWithinAbsoluteError (view.getShownWidgetAlpha (clockIndex), theme::state::draggedAlpha, 0.001f);
                renderSnapshot (view, "v32-layout-move");
                surface->mouseUp (event (*surface, to, grabPoint, true));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // the move commits after the mouse-up returns
                settle (engine, session);
                expectWithinAbsoluteError (view.getShownWidgetAlpha (clockIndex), 1.0f, 0.001f);
                const auto& clock = view.getShownWidgets()[(size_t) clockIndex];
                expect (clock.type == T::clock);
                expectEquals (clock.x, 20);   // grabbed one cell in: it lands one cell left of the pointer
                expectEquals (clock.y, 12);
                expect (! overlaps (view.getShownWidgets()));
            }

            // 4. A click (no drag) in the list adds the widget at the first place it fits.
            if (auto* l = view.getLayoutWidgetList())
            {
                const auto row = view.getLayoutListRow (T::clock);
                l->mouseDown (event (*l, row, row, false));
                l->mouseUp (event (*l, row, row, false));
                settle (engine, session);
                expectEquals ((int) view.getShownWidgets().size(), 5);
                expect (! overlaps (view.getShownWidgets()), "a click adds it where it fits");
                expectEquals (view.getShownWidgets().back().width, 4, "a click adds 4 x 2, as a drop does");
                expectEquals (view.getShownWidgets().back().height, 2);
            }

            // 5. A full stage: the drop box turns amber and nothing is added.
            session.setLayoutWidgets (id, layoutEditorWidgets());
            settle (engine, session);
            view.update (boardSnapshot());
            const auto before = view.getShownWidgets();
            dragFromList (T::tuner, at (6.0f, 9.0f), false);
            expect (view.isLayoutDropRefused(), "no free space: refused");
            renderSnapshot (view, "v32-layout-refused");
            list->mouseUp (event (*list, list->getLocalPoint (surface, at (6.0f, 9.0f)), view.getLayoutListRow (T::tuner), true));
            settle (engine, session);
            expect (view.getShownWidgets() == before, "nothing changed");

            // A click on a full stage adds nothing either (decided 19 Nov: never on top of another).
            {
                const auto row = view.getLayoutListRow (T::clock);
                list->mouseDown (event (*list, row, row, false));
                list->mouseUp (event (*list, row, row, false));
                settle (engine, session);
                expect (view.getShownWidgets() == before, "a click with no free place adds nothing");
            }

            // Off the stage the row's picture follows the pointer, the grabbed point under it.
            {
                const auto offStage = list->getLocalBounds().getCentre();   // list coordinates
                dragFromList (T::clock, surface->getLocalPoint (list, offStage), false);
                const auto& ghost = view.getLayoutDragImage();
                expect (ghost.isVisible(), "the dragged row's picture shows off the stage");
                expect (! view.getLayoutDropBox()->isVisible());
                expect (ghost.getBounds().contains (view.getLocalPoint (list, offStage)), "the picture is under the pointer");
                bool clicksSelf = true, clicksChildren = true;
                ghost.getInterceptsMouseClicks (clicksSelf, clicksChildren);
                expect (! clicksSelf && ! clicksChildren, "the picture never takes clicks");
                renderSnapshot (view, "v32-layout-drag-off-stage");
            }

            // Let go off the stage: nothing happens.
            list->mouseUp (event (*list, list->getLocalBounds().getCentre(), view.getLayoutListRow (T::clock), true));
            expect (! view.getLayoutDragImage().isVisible());
            expect (view.getShownWidgets() == before);

            view.setEditingLayout (false);
        }

        beginTest ("Stage: a musical, typing a cue number (stage-musical-goto.png)");
        {
            ShowSession session (engine);
            expect (session.openShow (makeMusicalShow (root)).wasOk());
            setUpStage (engine, session);
            settle (engine, session);
            session.goToCue (4);   // "Title Song"
            settle (engine, session);

            StageView view (engine, session);
            view.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);

            auto goTo = boardSnapshot();
            goTo.notice = "Go to cue 8_   (Enter to go, Esc to cancel)";

            for (int i = 0; i < 3; ++i)
                view.update (goTo);

            expect (hasContent (renderSnapshot (view, "stage-musical-goto")));
            renderSnapshot (view, "stage-musical-goto@2x", 2.0f);
        }

        engine.audioDeviceStopped();
    }
};

static StageBoardTests stageBoardTests;

} // namespace encore::test
