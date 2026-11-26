// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// A real menu on a real desktop, read back from the screen (Windows; CI runs on
// a desktop session): its rounded corners show what is behind them, never a
// square menu or a square shadow (Adrian, 20 Nov 2026: a "triangle" under each
// rounded corner).

#include <juce_gui_basics/juce_gui_basics.h>

#include "ScreenCapture.h"
#include "UiSnapshot.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class MenuWindowTests final : public juce::UnitTest
{
public:
    MenuWindowTests() : juce::UnitTest ("Menus on a real window", "Encore") {}

    void runTest() override
    {
        beginTest ("A menu's rounded corners show the window behind them");

        if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) == 0)
        {
            logMessage ("skipped: reads the screen on Windows only");
            return;
        }

        ScopedEncoreLook look;
        auto& mm = *juce::MessageManager::getInstance();

        // A bright window behind the menu, so any dark pixel at a corner shows.
        struct Backdrop final : juce::Component
        {
            Backdrop() { setOpaque (true); setName ("backdrop"); }
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xFFFF00FF)); }
        } backdrop;

        backdrop.setBounds (200, 150, 640, 480);
        backdrop.addToDesktop (0);
        backdrop.setVisible (true);
        backdrop.toFront (true);
        mm.runDispatchLoopUntil (300);

        juce::PopupMenu menu;
        menu.addItem (1, "Save now", true, false);
        menu.addItem (2, "Save as...", true, false);
        menu.addSeparator();
        menu.addItem (3, "Close show", true, false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ 360, 260, 1, 1 }));
        mm.runDispatchLoopUntil (600);

        auto* window = juce::ModalComponentManager::getInstance()->getModalComponent (0);
        expect (window != nullptr, "the menu opened");

        if (window == nullptr)
            return;

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto area = displays.logicalToPhysical (window->getScreenBounds());
        const auto scale = (float) area.getWidth() / (float) window->getWidth();
        const auto pad = juce::roundToInt (8.0f * scale);
        const auto shot = area.expanded (pad);
        std::string error;
        const auto pixels = screen::capture (shot.getX(), shot.getY(), shot.getWidth(), shot.getHeight(), error);

        juce::PopupMenu::dismissAllActiveMenus();
        mm.runDispatchLoopUntil (100);
        backdrop.removeFromDesktop();

        if (pixels.empty())
        {
            const auto onCi = juce::SystemStats::getEnvironmentVariable ("CI", {}).isNotEmpty();
            expect (! onCi, "screen capture failed on CI: " + juce::String (error));
            logMessage ("skipped: " + juce::String (error));
            return;
        }

        // At (x, y) physical pixels from the menu window's top left.
        const auto at = [&] (int x, int y)
        {
            return juce::Colour (pixels[(size_t) ((y + pad) * shot.getWidth() + (x + pad))]);
        };

        const auto w = area.getWidth(), h = area.getHeight();
        const auto inset = juce::roundToInt (2.0f * scale);   // well outside the 10 px rounded corner
        const juce::Colour corners[] { at (inset, inset), at (w - 1 - inset, inset),
                                       at (inset, h - 1 - inset), at (w - 1 - inset, h - 1 - inset) };
        const auto middle = at (w / 2, h / 2);

        juce::String report;
        report << "scale " << scale << ", menu " << area.toString() << ", middle " << middle.toDisplayString (false)
               << ", corners";

        for (auto& c : corners)
            report << " " << c.toDisplayString (false);

        const auto o = juce::roundToInt (3.0f * scale);
        report << ", outside: corner " << at (w - 1 + o, h - 1 + o).toDisplayString (false)
               << " bottom " << at (w / 2, h - 1 + o).toDisplayString (false)
               << " right " << at (w - 1 + o, h / 2).toDisplayString (false);

        logMessage (report);
        expect (false, "DIAG " + report);

        // A corner is the backdrop, at most darkened by the shadow: still magenta
        // (red and blue well above green). A square menu or shadow there is grey.
        const auto showsBackdrop = [] (juce::Colour c)
        {
            return c.getRed() > c.getGreen() * 2 + 20 && c.getBlue() > c.getGreen() * 2 + 20;
        };

        for (int i = 0; i < 4; ++i)
            expect (showsBackdrop (corners[i]), "corner " + juce::String (i) + " shows the window behind it: " + report);

        // The menu itself is the design's menu fill (98 % over magenta).
        expect (middle.getGreen() < 60 && middle.getRed() < 80 && middle.getBlue() < 80, "the menu is the menu colour: " + report);
    }
};

static MenuWindowTests menuWindowTests;

} // namespace encore::test
