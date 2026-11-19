// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// A tour of the UI for a showcase video: only runs with ENCORE_SHOWCASE=<folder>.
// Drives the real main window (the board's show, a fake audio device) through
// Edit, the full list, menus, Stage, the tuner, the layout editor, settings and
// the pre-show check, and writes one PNG per step plus tour.txt
// (file, seconds to hold, where the pointer goes, click or not, caption).

#include "BoardFixtures.h"
#include "UiSnapshot.h"

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "ui/ChainStrip.h"
#include "ui/StageView.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class ShowcaseTests final : public juce::UnitTest
{
public:
    ShowcaseTests() : juce::UnitTest ("Showcase tour (ENCORE_SHOWCASE)", "Encore") {}

    static void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    template <typename T>
    static T* findOf (juce::Component& parent, std::function<bool (T&)> match = {})
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c); t != nullptr && t->isVisible() && (match == nullptr || match (*t)))
                return t;

            if (auto* found = findOf<T> (*c, match))
                return found;
        }

        return nullptr;
    }

    void runTest() override
    {
        const auto folderPath = juce::SystemStats::getEnvironmentVariable ("ENCORE_SHOWCASE", {});

        if (folderPath.isEmpty())
            return;   // only on request

        beginTest ("Tour");
        ScopedEncoreLook look;
        const juce::File out (folderPath);
        out.deleteRecursively();
        out.createDirectory();
        juce::String tour;

        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();
        const auto showFolder = makeBoardShow (root);

        AppSettings settings (root.getChildFile ("Settings"));
        settings.setLastShowFolder (showFolder);
        const auto setup = boardDeviceSetup();
        settings.setAudioDeviceState (&setup);

        MainComponent main (settings, std::make_unique<BoardDeviceType>());
        main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
        pump (1500);

        // ENCORE_SHOWCASE_SCALE=2: frames at twice the size (sharp crops for a promo video).
        const auto scale = juce::jlimit (1.0f, 4.0f, juce::SystemStats::getEnvironmentVariable ("ENCORE_SHOWCASE_SCALE", "1").getFloatValue());
        int frame = 0;
        const auto centreOf = [&main] (juce::Component* c)
        {
            return c != nullptr ? main.getLocalArea (c->getParentComponent(), c->getBounds()).getCentre() : juce::Point<int> (-1, -1);
        };
        const auto button = [&main] (const juce::String& text) -> juce::Component*
        {
            return findOf<juce::Button> (main, [text] (juce::Button& b) { return b.getButtonText() == text; });
        };
        const auto shot = [&] (const juce::String& caption, double hold, juce::Point<int> pointer = { -1, -1 }, bool click = false,
                               std::function<void (juce::Image&)> overlay = {})
        {
            pump (350);
            auto image = main.createComponentSnapshot (main.getLocalBounds(), false, scale);

            if (overlay != nullptr)
                overlay (image);

            const auto file = juce::String (frame++).paddedLeft ('0', 3) + ".png";
            juce::FileOutputStream stream (out.getChildFile (file));
            juce::PNGImageFormat().writeImageToStream (image, stream);
            tour << file << "\t" << hold << "\t" << pointer.x << "\t" << pointer.y << "\t" << (click ? 1 : 0) << "\t" << caption << "\n";
        };
        const auto key = [&main] (int code, juce::juce_wchar ch = 0) { main.keyPressed (juce::KeyPress (code, juce::ModifierKeys(), ch)); };

        // Edit view
        shot ("Edit: the set list, the signal chain, the amp on its stage and the inspector", 4.0);

        key (juce::KeyPress::spaceKey, ' ');
        shot ("Space: the next song; the LCD follows", 2.5, { 120, 148 }, true);
        key (juce::KeyPress::spaceKey, ' ');
        shot ("...and the next", 2.0, { 120, 175 }, true);

        if (auto* chain = findOf<ChainStrip> (main))
        {
            chain->selectBlock (2);
            shot ("Click a block: its controls and inspector", 3.0, { 504, 185 }, true);
            chain->selectBlock (3);
            shot ("The EQ", 2.5, { 574, 185 }, true);
            chain->selectBlock (0);
            shot ("Back to the amp", 2.0, { 337, 185 }, true);
        }

        // The full list
        key (juce::KeyPress::F6Key);
        shot ("F6: the full cue list for editing", 3.5, centreOf (button ("Full list")), true);
        key (juce::KeyPress::downKey);
        key (juce::KeyPress::downKey);
        shot ("Keys move through it", 2.0);
        key (juce::KeyPress::F6Key);
        shot ("F6: back to patches", 1.5);

        // The Show menu (menus are windows of their own: drawn into the frame)
        {
            auto* showButton = button ("Show");
            const auto at = showButton != nullptr ? main.getLocalArea (showButton->getParentComponent(), showButton->getBounds()).getBottomLeft()
                                                  : juce::Point<int>();
            const auto menu = main.buildShowMenu();
            shot ("The Show menu", 3.0, centreOf (showButton), true, [&] (juce::Image& image)
            {
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                paintMenu (g, main.getLookAndFeel(), menu, at.translated (-theme::size::menuPadding, 0));
            });
        }

        // Settings and the pre-show check
        main.openSettings (SettingsWindow::Tab::audio);
        pump (400);
        shot ("Settings: Audio", 4.0, centreOf (button ("Audio...")), true);
        main.openSettings (SettingsWindow::Tab::midi);
        shot ("Settings: MIDI mappings for foot controllers", 4.0, { 761, 140 }, true);
        main.closeSettings();
        main.openPreShowCheck();
        pump (1100);
        shot ("F9: the pre-show check, worst first, with fixes", 4.5);
        main.closePreShowCheck();

        // Go live (Encore always starts muted), then Stage
        if (auto* mute = dynamic_cast<juce::Button*> (button ("MUTED")))
        {
            const auto at = centreOf (mute);
            mute->triggerClick();
            shot ("Unmute: live", 2.0, at, true);
        }

        key (juce::KeyPress::F5Key);
        shot ("Stage: big and readable from across the stage", 4.0, centreOf (button ("Stage")), true);
        key (juce::KeyPress::spaceKey, ' ');
        shot ("The next song", 2.5);

        if (auto* stage = findOf<StageView> (main))
        {
            key ('t', 't');
            shot ("T: the tuner", 3.0, centreOf (button ("Tuner")), true);
            key ('t', 't');
            shot ("Back to the stage", 1.5);

            stage->setEditingLayout (true);
            shot ("Layout: arrange the stage widgets on a grid", 3.5, centreOf (button ("Layout")), true);
            stage->selectLayoutWidget (0);
            shot ("Select a widget: text size, duplicate, remove", 3.5, { 560, 300 }, true);
            stage->setEditingLayout (false);
            shot ("Done: back on stage", 2.5, centreOf (button ("Done")), true);
        }

        key (juce::KeyPress::F5Key);
        shot ("F5: back to Edit", 3.0);

        out.getChildFile ("tour.txt").replaceWithText (tour);
        expect (frame > 10);
    }
};

static ShowcaseTests showcaseTests;

} // namespace encore::test
