// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// The two-finger tap: what reaches the app, and when it's a right click.
// (The Windows glue in TouchGestures.cpp applies these answers; it needs a touch screen.)

#include "ui/TouchGestures.h"
#include "TouchInjection.h"

namespace encore::test
{

class TouchGestureTests final : public juce::UnitTest
{
public:
    TouchGestureTests() : juce::UnitTest ("Touch: two-finger right click", "Encore") {}

    void runTest() override
    {
        using A = TwoFingerTapDetector::Action;
        const juce::Point<float> p1 { 100.0f, 100.0f }, p2 { 160.0f, 110.0f };

        beginTest ("One finger: everything passes, a hold is a hold, never a right click");
        {
            TwoFingerTapDetector d;
            bool right = true;
            expect (d.down (1, p1, 0, true) == A::pass);
            expect (d.move (1, p1.translated (2.0f, 0.0f)) == A::pass);
            expect (d.up (1, p1, 2000, right) == A::pass);
            expect (! right);
        }

        beginTest ("Two fingers tapped together: the first press is cancelled, the second never arrives, a right click where the first was");
        {
            TwoFingerTapDetector d;
            bool right = false;
            expect (d.down (1, p1, 0, true) == A::pass);
            expect (d.down (2, p2, 80, true) == A::cancelFirstAndSwallow);
            expect (d.move (1, p1.translated (3.0f, 3.0f)) == A::swallow, "the first finger's moves would press again");
            expect (d.move (2, p2) == A::swallow);
            expect (d.up (2, p2, 200, right) == A::swallow);
            expect (! right, "not until both are up");
            expect (d.up (1, p1, 220, right) == A::pass, "the first lift reaches the app as a hover");
            expect (right);
            expect (d.getFirstPosition() == p1);
        }

        beginTest ("Lifted in the other order: the same");
        {
            TwoFingerTapDetector d;
            bool right = false;
            d.down (1, p1, 0, true);
            d.down (2, p2, 50, true);
            expect (d.up (1, p1, 150, right) == A::pass);
            expect (d.up (2, p2, 170, right) == A::swallow);
            expect (right);
        }

        beginTest ("Not a tap: a late second finger, a move, a long press, a third finger, the live Stage");
        {
            bool right = false;

            {
                TwoFingerTapDetector d;   // one finger held (a drag), then a second: ordinary multi-touch
                d.down (1, p1, 0, true);
                expect (d.down (2, p2, 900, true) == A::pass);
                expect (d.up (2, p2, 1000, right) == A::pass && ! right);
                expect (d.up (1, p1, 1100, right) == A::pass && ! right);
            }
            {
                TwoFingerTapDetector d;   // the first finger already moving: a drag
                d.down (1, p1, 0, true);
                d.move (1, p1.translated (40.0f, 0.0f));
                expect (d.down (2, p2, 100, true) == A::pass);
            }
            {
                TwoFingerTapDetector d;   // two fingers that slide apart (a pinch): presses cancelled, no right click
                d.down (1, p1, 0, true);
                d.down (2, p2, 60, true);
                d.move (2, p2.translated (80.0f, 0.0f));
                d.up (1, p1, 200, right);
                d.up (2, p2.translated (80.0f, 0.0f), 220, right);
                expect (! right);
            }
            {
                TwoFingerTapDetector d;   // held too long
                d.down (1, p1, 0, true);
                d.down (2, p2, 60, true);
                d.up (1, p1, 900, right);
                d.up (2, p2, 950, right);
                expect (! right);
            }
            {
                TwoFingerTapDetector d;   // a third finger spoils it
                d.down (1, p1, 0, true);
                d.down (2, p2, 60, true);
                expect (d.down (3, p2.translated (50.0f, 0.0f), 90, true) == A::pass);
                d.up (1, p1, 200, right);
                d.up (2, p2, 210, right);
                expect (! right);
            }
            {
                TwoFingerTapDetector d;   // the live Stage: two ordinary touches
                d.down (1, p1, 0, false);
                expect (d.down (2, p2, 60, false) == A::pass);
                expect (d.up (2, p2, 120, right) == A::pass && ! right);
                expect (d.up (1, p1, 130, right) == A::pass && ! right);
            }
        }

        beginTest ("After a gesture, the next single touch is ordinary again");
        {
            TwoFingerTapDetector d;
            bool right = false;
            d.down (1, p1, 0, true);
            d.down (2, p2, 50, true);
            d.up (1, p1, 100, right);
            d.up (2, p2, 120, right);
            expect (right);
            expect (d.down (7, p2, 5000, true) == A::pass);
            expect (d.move (7, p2) == A::pass);
            expect (d.up (7, p2, 5100, right) == A::pass && ! right);
        }

        // On a real window (Windows: CI runs on a desktop session). A menu that a
        // right click opens must stay open (Adrian, 20 Nov: on a selected block in
        // the signal chain it opened and closed again at once).
        if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) != 0)
        {
            beginTest ("A menu opened by the synthetic right click stays open");
            checkMenuStaysOpen (false);

            beginTest ("A menu opened by a two-finger tap (injected touches) stays open");
            checkMenuStaysOpen (true);
        }
    }

private:
    /** Opens a menu on a right press, as ChainStrip does for a block. */
    struct MenuTarget final : juce::Component
    {
        MenuTarget() { setName ("target"); setWantsKeyboardFocus (true); }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::darkgrey); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            log.add (describe ("down", e));

            if (! e.mods.isPopupMenu())
                return;

            juce::PopupMenu menu;
            menu.addItem (1, "One");
            menu.addItem (2, "Two");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                          .withTargetScreenArea (localAreaToGlobal (getLocalBounds().reduced (40))));
        }

        void mouseUp (const juce::MouseEvent& e) override { log.add (describe ("up", e)); }

        static juce::String describe (const char* what, const juce::MouseEvent& e)
        {
            return juce::String (what) + (e.source.isTouch() ? " touch" : " mouse") + juce::String (e.source.getIndex())
                 + (e.mods.isRightButtonDown() ? " right" : e.mods.isLeftButtonDown() ? " left" : "")
                 + " at " + e.getPosition().toString();
        }

        juce::StringArray log;
    };

    static int openMenus() { return juce::ModalComponentManager::getInstance()->getNumModalComponents(); }

    static juce::String state()
    {
        juce::String s;
        s << "foreground " << (juce::Process::isForegroundProcess() ? "yes" : "no");
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        s << ", focused " << (focused != nullptr ? focused->getName().quoted() : juce::String ("none"));

        for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        {
            auto* peer = juce::ComponentPeer::getPeer (i);
            s << ", peer " << peer->getComponent().getName().quoted() << (peer->isFocused() ? " focused" : "");
        }

        s << ", menus " << openMenus();

        for (auto& source : juce::Desktop::getInstance().getMouseSources())
            s << " | " << (source.isTouch() ? "touch" : "mouse") << source.getIndex()
              << " " << source.getScreenPosition().roundToInt().toString() << (source.isDragging() ? " down" : "");

        return s;
    }

    void checkMenuStaysOpen (bool byTouch)
    {
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        juce::Component window;
        window.setName ("window");
        MenuTarget target;
        window.addAndMakeVisible (target);
        window.setBounds (240, 200, 520, 320);
        target.setBounds (window.getLocalBounds());
        window.addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        window.setVisible (true);
        window.toFront (true);
        target.grabKeyboardFocus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        TwoFingerRightClick twoFingers { [] { return true; } };
        twoFingers.attachTo (window);

        const auto centre = target.localPointToGlobal (target.getLocalBounds().getCentre()).toFloat();
        logMessage ("before: " + state());

        if (! byTouch)
        {
            rightClickAt (window, centre);
        }
        else
        {
            if (! TwoFingerRightClick::isAvailable())
            {
                logMessage ("skipped: the JUCE touch patch isn't in this build");
                return;
            }

            using touch::Contact;
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto p1 = displays.logicalToPhysical (centre).roundToInt();
            const auto p2 = displays.logicalToPhysical (centre.translated (60.0f, 0.0f)).roundToInt();
            const auto c = [] (int id, juce::Point<int> p, Contact::Phase phase) { return Contact { id, p.x, p.y, phase }; };
            std::string error;

            static const bool ready = touch::initialise (2, error);

            const std::vector<std::vector<Contact>> frames {
                { c (0, p1, Contact::Phase::down) },
                { c (0, p1, Contact::Phase::move), c (1, p2, Contact::Phase::down) },
                { c (0, p1, Contact::Phase::move), c (1, p2, Contact::Phase::move) },
                { c (0, p1, Contact::Phase::up),   c (1, p2, Contact::Phase::up) } };

            for (const auto& frame : frames)
            {
                if (! ready || ! touch::inject (frame, error))
                {
                    logMessage ("skipped: " + juce::String (error) + " (touch injection needs a desktop session)");
                    return;
                }

                juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            }
        }

        // Watch it for a second.
        int openedAt = -1, closedAt = -1;
        juce::String lastOpen;

        for (int ms = 0; ms < 1000; ms += 5)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            const auto open = openMenus() > 0;

            if (open && openedAt < 0)
                openedAt = ms;

            if (open)
                lastOpen = state();

            if (! open && openedAt >= 0 && closedAt < 0)
                closedAt = ms;
        }

        logMessage ("events: " + target.log.joinIntoString ("; "));
        logMessage ("opened at " + juce::String (openedAt) + " ms, closed at " + juce::String (closedAt) + " ms");
        logMessage ("while open: " + lastOpen);
        logMessage ("after: " + state());

        expect (target.log.joinIntoString (";").contains ("down mouse0 right"), "the right press arrived");
        expect (openedAt >= 0, "the menu opened");
        expect (closedAt < 0, "the menu stayed open");

        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        window.removeFromDesktop();
    }
};

static TouchGestureTests touchGestureTests;

} // namespace encore::test
