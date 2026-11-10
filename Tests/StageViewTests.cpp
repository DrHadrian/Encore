// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "FakeDevice.h"
#include "TestSignals.h"

#include "ui/StageView.h"

namespace encore::test
{

/** The stage view, drawn off-screen: every widget type, edit mode, the tuner
    and the lock, at laptop and small sizes. Set ENCORE_UI_SNAPSHOTS to a
    folder to keep the images (handy when changing the look). */
class StageViewTests final : public juce::UnitTest
{
public:
    StageViewTests() : juce::UnitTest ("Stage view", "Encore") {}

    juce::Image render (juce::Component& c, const juce::String& fileName)
    {
        auto image = c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);

        if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ENCORE_UI_SNAPSHOTS", {}); folder.isNotEmpty())
        {
            juce::File (folder).createDirectory();
            juce::FileOutputStream out (juce::File (folder).getChildFile (fileName + ".png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        return image;
    }

    static bool hasContent (const juce::Image& image)
    {
        // Something brighter than the background was drawn.
        for (int y = 0; y < image.getHeight(); y += 7)
            for (int x = 0; x < image.getWidth(); x += 7)
                if (image.getPixelAt (x, y).getBrightness() > 0.6f)
                    return true;

        return false;
    }

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

    void runTest() override
    {
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-stage-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        FakeDevice device;
        AudioEngine engine;
        engine.audioDeviceAboutToStart (&device);

        {
            ShowSession session (engine);
            expect (session.newShow (temp.getChildFile ("Stage.encore"), "Stage test").wasOk());

            const auto patch = session.getShow().patches.front().id;
            session.addCue (patch, -1);
            session.addCue (patch, 0);
            session.renameCue (0, "Overture");
            session.renameCue (1, "Think of me");

            for (int i = 0; i < 200 && session.isBuilding(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            session.goToCue (0);
            play (engine, session);

            // Every widget type in one layout.
            std::vector<StageWidget> all;
            int i = 0;

            for (auto type : allStageWidgetTypes())
            {
                all.push_back ({ type, (i % 4) * 6, (i / 4) * 4, 6, 4, 1.0f, type == StageWidgetType::text ? "Watch the MD" : "" });
                ++i;
            }

            StageView view (engine, session);
            view.setBounds (0, 0, 1366, 768);

            beginTest ("The default layout draws");
            {
                view.update ({});
                expect (hasContent (render (view, "stage-default")));
            }

            beginTest ("Every widget type draws, at any size");
            {
                const auto id = session.addLayout ("All", {});
                session.setLayoutWidgets (id, all);
                session.setCueLayout (0, id);
                StageSnapshot snapshot;
                snapshot.cpu = 0.12;
                snapshot.inputPeak = 0.3f;
                snapshot.tuner = { true, 110.0f, 45, -4.0f, 0.1f };
                view.update (snapshot);
                expect (hasContent (render (view, "stage-all-widgets")));

                view.setBounds (0, 0, 640, 400);
                view.update (snapshot);
                expect (hasContent (render (view, "stage-all-widgets-small")));
                view.setBounds (0, 0, 1366, 768);
            }

            beginTest ("Per-cue layouts follow the cue");
            {
                session.goToCue (1);
                play (engine, session);
                expectEquals (session.getCurrentCue(), 1);
                view.update ({});
                expect (hasContent (render (view, "stage-cue-2-default")));
            }

            beginTest ("Edit mode, tuner overlay and lock draw");
            {
                view.setEditingLayout (true);
                view.update ({});
                expect (hasContent (render (view, "stage-editing")));
                view.setEditingLayout (false);

                engine.setTunerOpen (true);
                StageSnapshot tuning;
                tuning.tuner = { true, 329.0f, 64, 1.0f, 0.1f };
                view.update (tuning);
                expect (hasContent (render (view, "stage-tuner")));
                engine.setTunerOpen (false);

                view.setLocked (true);
                view.update ({});
                expect (view.isLocked());
                view.setEditingLayout (true);
                expect (! view.isEditingLayout(), "no layout editing while locked");
                expect (hasContent (render (view, "stage-locked")));
                view.setLocked (false);
            }
        }

        engine.audioDeviceStopped();
        temp.deleteRecursively();
    }
};

static StageViewTests stageViewTests;

} // namespace encore::test
