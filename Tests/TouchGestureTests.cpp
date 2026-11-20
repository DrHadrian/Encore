// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// The two-finger tap: what reaches the app, and when it's a right click.
// (The Windows glue in TouchGestures.cpp applies these answers; it needs a touch screen.)

#include "ui/TouchGestures.h"

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
    }
};

static TouchGestureTests touchGestureTests;

} // namespace encore::test
