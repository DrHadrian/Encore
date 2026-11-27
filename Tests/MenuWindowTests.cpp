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
#include "ui/theme/ShapedShadow.h"

namespace encore::test
{

class MenuWindowTests final : public juce::UnitTest
{
public:
    MenuWindowTests() : juce::UnitTest ("Menus on a real window", "Encore") {}

    void runTest() override
    {
        beginTest ("A rendered menu shadow is round at the corners and clear inside the menu");
        checkRenderedShadow();

        if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) == 0)
        {
            logMessage ("skipped: the screen tests run on Windows only");
            return;
        }

        beginTest ("A real menu's rounded corners: the window behind, under the menu's shadow");
        checkOnScreen (1.0f);

        beginTest ("The same at 150 % (Encore's own scale)");
        checkOnScreen (1.5f);
    }

private:
    void checkRenderedShadow()
    {
        const theme::ShapedShadow::Look look { 10.0f, juce::Colour (0x99000000), 30, { 0, 12 } };
        const auto margins = theme::ShapedShadow::marginsFor (look);
        expectEquals (margins.getLeft(), 46);   // 1.5 × the blur, + 1
        expectEquals (margins.getTop(), 34);
        expectEquals (margins.getBottom(), 58);

        for (const auto scale : { 1.0f, 1.5f, 2.0f })
        {
            const auto image = theme::ShapedShadow::render ({ 200, 120 }, look, scale);
            expectEquals (image.getWidth(), juce::roundToInt ((200.0f + 92.0f) * scale));

            // At a point in the owner's logical pixels (0, 0 = its top left).
            const auto alpha = [&] (float x, float y)
            {
                return image.getPixelAt (juce::roundToInt ((x + 46.0f) * scale), juce::roundToInt ((y + 34.0f) * scale)).getAlpha();
            };

            const auto where = " at " + juce::String (scale) + "x";
            expectEquals ((int) alpha (100.0f, 60.0f), 0, "clear inside the menu" + where);
            expectEquals ((int) alpha (12.0f, 12.0f), 0, "clear just inside a corner" + where);
            expectGreaterThan ((int) alpha (100.0f, 125.0f), 40, "dark under the menu" + where);

            // The corner the rounded shape leaves out is shadowed like the edges next
            // to it (no lighter triangle), and the shadow rounds off past the corner.
            const auto inCorner = (int) alpha (198.5f, 118.5f);
            expectGreaterThan (inCorner, 40, "shadow in the rounded-off corner" + where);
            expectWithinAbsoluteError (inCorner, (int) alpha (201.0f, 100.0f), 40, "corner like the edge beside it" + where);
            expectLessThan ((int) alpha (215.0f, 140.0f), (int) alpha (100.0f, 140.0f), "the shadow rounds off past the corner" + where);
        }
    }

    void checkOnScreen (float globalScale)
    {
        auto& desktop = juce::Desktop::getInstance();
        desktop.setGlobalScaleFactor (globalScale);
        const juce::ScopeGuard resetScale { [&] { desktop.setGlobalScaleFactor (1.0f); } };

        ScopedEncoreLook look;
        auto& mm = *juce::MessageManager::getInstance();

        // A bright window behind the menu, so any dark pixel at a corner shows.
        struct Backdrop final : juce::Component
        {
            Backdrop() { setOpaque (true); setName ("backdrop"); }
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xFFFF00FF)); }
        } backdrop;

        backdrop.setBounds (200, 150, 640, 480);
        // Always on top, as the menu: another test's window (ctest runs groups side by
        // side) once covered it, and the corners showed that window instead.
        backdrop.setAlwaysOnTop (true);
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

        // The menu window exists as soon as it's shown. A menu closes itself when the
        // app isn't in front and the mouse isn't over it, and CI can't always bring
        // the test to the front (once it closed before it was read, Release, 150 %):
        // the mouse goes onto the menu's top padding, over no item.
        auto* window = juce::ModalComponentManager::getInstance()->getModalComponent (0);
        expect (window != nullptr, "the menu opened");

        if (window == nullptr)
            return;

        const auto parkMouse = window->getScreenBounds().toFloat().getCentre().withY ((float) window->getScreenY() + 2.0f);
        desktop.setMousePosition (parkMouse.roundToInt());
        mm.runDispatchLoopUntil (600);
        expect (juce::ModalComponentManager::getInstance()->getModalComponent (0) == window, "the menu stayed open");

        if (juce::ModalComponentManager::getInstance()->getModalComponent (0) != window)
            return;

        auto* layer = theme::ShapedShadow::findLayer (*window);
        expect (layer != nullptr && layer->isShowing() && layer->isOnDesktop(), "the menu has its own shadow window");

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto area = displays.logicalToPhysical (window->getScreenBounds());
        const auto scale = (float) area.getWidth() / (float) window->getWidth();
        const auto pad = juce::roundToInt (8.0f * scale);
        const auto shot = area.expanded (pad);
        std::string error;
        const auto pixels = screen::capture (shot.getX(), shot.getY(), shot.getWidth(), shot.getHeight(), error);

        const juce::ScopeGuard close { [&]
        {
            juce::PopupMenu::dismissAllActiveMenus();
            mm.runDispatchLoopUntil (100);
            backdrop.removeFromDesktop();
        } };

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
        const auto middle = at (w - 1 - juce::roundToInt (3.0f * scale), h / 2);   // in the menu's 4 px padding, clear of text

        juce::String report;
        report << "scale " << scale << ", menu " << area.toString() << ", menu fill " << middle.toDisplayString (false)
               << ", corners";

        for (auto& c : corners)
            report << " " << c.toDisplayString (false);

        const auto o = juce::roundToInt (3.0f * scale);
        report << ", outside: corner " << at (w - 1 + o, h - 1 + o).toDisplayString (false)
               << " bottom " << at (w / 2, h - 1 + o).toDisplayString (false)
               << " right " << at (w - 1 + o, h / 2).toDisplayString (false);

        logMessage (report);

        // A corner is the backdrop, at most darkened by the shadow: still magenta
        // (red and blue well above green). A square menu or shadow there is grey.
        const auto showsBackdrop = [] (juce::Colour c)
        {
            return c.getRed() > c.getGreen() * 2 + 20 && c.getBlue() > c.getGreen() * 2 + 20;
        };

        for (int i = 0; i < 4; ++i)
            expect (showsBackdrop (corners[i]), "corner " + juce::String (i) + " shows the window behind it: " + report);

        // The shadow (12 px down) reaches under the bottom corners as under the edges
        // beside them: no unshadowed square corner (what JUCE's square shadow left).
        expect (corners[2].getRed() < 0xE8 && corners[3].getRed() < 0xE8, "the shadow fills the bottom corners: " + report);

        // The menu itself is the design's menu fill (98 % over magenta).
        expect (middle.getGreen() < 50 && middle.getRed() < 60 && middle.getBlue() < 60, "the menu is the menu colour: " + report);
    }
};

static MenuWindowTests menuWindowTests;

} // namespace encore::test
