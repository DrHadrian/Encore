// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Design v3 hardening (JUCE-ARCHITECTURE.md §6-7): every screen with a
// 300-cue show in 40 sections and very long names, and with an empty show,
// from 640 x 480 to 4K, without asserting; paint times logged (the budgets
// are for Release builds, so they are reported, not enforced, here).

#include "BoardFixtures.h"
#include "UiSnapshot.h"

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class HardeningTests final : public juce::UnitTest
{
public:
    HardeningTests() : juce::UnitTest ("Design v3 hardening", "Encore") {}

    static void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    static juce::File makeHugeShow (const juce::File& parent)
    {
        auto show = makeDefaultShow ("A show with a very long name that goes on and on and on, to see where it is cut");
        show.showTemplate = ShowTemplate::musical;
        const auto patch = show.patches[0].id;
        show.patches[0].name = "A patch with a name long enough to need cutting in every list it appears in";

        for (int s = 0; s < 40; ++s)
        {
            auto section = RunItem::makeSection ("Scene " + juce::String (s + 1) + " - a section name that is far too long for any pane", "scene");

            for (int c = 0; c < 8; ++c)
            {
                auto cue = RunItem::makeCue ({}, "Cue " + juce::String (s * 8 + c + 1) + " with a long, long name that keeps going past the edge", patch);
                cue.note = "A note that is also quite long, so it has to wrap onto several lines on stage and still fit somewhere";
                section.children.push_back (cue);
            }

            show.runningOrder.children.push_back (section);
        }

        const auto folder = parent.getChildFile ("Huge Show");
        ShowFolder::create (folder, show);
        return folder;
    }

    static juce::File makeEmptyShow (const juce::File& parent)
    {
        auto show = makeDefaultShow ("Empty");
        show.runningOrder.children.clear();
        const auto folder = parent.getChildFile ("Empty Show");
        ShowFolder::create (folder, show);
        return folder;
    }

    /** Paints the whole window once and returns the time it took (ms). */
    static double timePaint (juce::Component& c)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true);
        juce::Graphics g (image);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        c.paintEntireComponent (g, false);
        return juce::Time::getMillisecondCounterHiRes() - start;
    }

    void runScreens (const juce::File& showFolder, const juce::String& label, const juce::File& root)
    {
        AppSettings settings (root.getChildFile ("Settings " + label));
        settings.setLastShowFolder (showFolder);
        const auto setup = boardDeviceSetup();
        settings.setAudioDeviceState (&setup);

        MainComponent main (settings, std::make_unique<BoardDeviceType>());
        main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
        pump (1500);

        const auto press = [&main] (int code) { main.keyPressed (juce::KeyPress (code)); pump (100); };
        double worst = 0.0;

        const juce::Point<int> sizes[] = { { 640, 480 }, { 1024, 640 }, { 1920, 1080 }, { 3840, 2160 }, { 1440, 900 } };

        // Edit, the full list, Stage (the check and the settings on top), Edit again.
        for (int screen = 0; screen < 4; ++screen)
        {
            if (screen == 1) press (juce::KeyPress::F6Key);       // the full list
            if (screen == 2) { press (juce::KeyPress::F6Key); press (juce::KeyPress::F5Key); }   // back, then Stage

            for (auto size : sizes)
            {
                main.setSize (size.x, size.y);
                pump (60);
                const auto ms = timePaint (main);
                worst = juce::jmax (worst, ms);

                if (size.x == 1920)
                    logMessage (label + ", screen " + juce::String (screen) + ", 1920 x 1080: " + juce::String (ms, 1) + " ms");
            }

            expect (hasContent (renderSnapshot (main, "hardening-" + label + "-" + juce::String (screen))));

            if (screen == 2)
            {
                main.openPreShowCheck();
                pump (100);
                main.openSettings (SettingsWindow::Tab::midi);
                pump (100);
                worst = juce::jmax (worst, timePaint (main));
                main.closeSettings();
                main.closePreShowCheck();
                press (juce::KeyPress::F5Key);   // back to Edit
            }
        }

        logMessage (label + ": slowest whole-window paint " + juce::String (worst, 1) + " ms (Debug)");
    }

    void runTest() override
    {
        ScopedEncoreLook look;
        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();

        beginTest ("300 cues in 40 sections, long names, every screen and size");
        runScreens (makeHugeShow (root), "huge", root);

        beginTest ("An empty show, every screen and size");
        runScreens (makeEmptyShow (root), "empty", root);
    }
};

static HardeningTests hardeningTests;

} // namespace encore::test
