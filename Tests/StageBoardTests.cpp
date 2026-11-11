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
    static void setUpStage (AudioEngine& engine, ShowSession& session)
    {
        const auto id = session.addLayout ("Default", {});
        session.setLayoutWidgets (id, boardStageWidgets());
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

            beginTest ("Stage: the interface lost (stage-interface-lost.png)");
            {
                auto lost = boardSnapshot();
                lost.audioProblem = "AUDIO INTERFACE LOST - reconnecting to Focusrite USB ASIO...";
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
