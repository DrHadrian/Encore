// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace encore
{

/** Decides what a two-finger tap is, from a window's raw touch messages, before
    the app sees them (TwoFingerRightClick applies the answers on Windows).

    A second finger that lands within `secondFingerMs` of the first, while the
    first hasn't moved, starts a gesture: the first finger's press is cancelled
    (nothing it touched clicks), and neither finger reaches the app again until
    it's lifted. When both are up, quickly and without either moving far, it's
    a right click where the first finger was. Everything else passes through.
    Times in ms, positions in any one unit (physical pixels on Windows). */
class TwoFingerTapDetector
{
public:
    static constexpr juce::uint32 secondFingerMs = 300;   // the second finger lands this soon after the first
    static constexpr juce::uint32 maxTapMs = 700;         // both lifted within this, from the first touch
    static constexpr float maxMove = 24.0f;               // further than this and it's not a tap

    enum class Action
    {
        pass,                 // hand the message to the app as usual
        swallow,              // the app never sees it
        cancelFirstAndSwallow // second finger down: cancel the first finger's press, swallow this one
    };

    /** `allowed`: whether a gesture may start now (not on the live Stage). */
    Action down (int pointerId, juce::Point<float> at, juce::uint32 timeMs, bool allowed);
    Action move (int pointerId, juce::Point<float> at);

    /** `rightClick` is set when this lift ends a two-finger tap. */
    Action up (int pointerId, juce::Point<float> at, juce::uint32 timeMs, bool& rightClick);

    /** Where the first finger was: where a right click goes. */
    juce::Point<float> getFirstPosition() const noexcept { return first.start; }

    /** The first finger lifts in a gesture: the app gets that lift, at no-buttons
        (its press was already cancelled, so it only hovers). */
    bool isFirstPointer (int pointerId) const noexcept { return first.id == pointerId; }

    bool isInGesture() const noexcept { return gesture; }

private:
    struct Finger
    {
        int id = -1;
        juce::Point<float> start;
        juce::uint32 downAt = 0;
        bool down = false, moved = false;
    };

    Finger first, second;
    bool gesture = false, spoiled = false;

    void reset() noexcept;
};

/** A mouse right click at a point on the screen (logical coordinates), as if the
    mouse had made it: the press, then the release. */
void rightClickAt (juce::Component& window, juce::Point<float> screen);

/** On Windows, a two-finger tap on the main window is a right click where the
    first finger was, wherever right clicks open menus, except on the live
    Stage (where widgets act the moment a finger lands). Holding one finger
    still stays a left press (system::disableTouchPressAndHold). Elsewhere it
    does nothing. */
class TwoFingerRightClick final
{
public:
    /** `allowed` is asked when a second finger lands. */
    explicit TwoFingerRightClick (std::function<bool()> allowed);
    ~TwoFingerRightClick();

    /** The window whose touches it reads (JUCE hands them over first, through
        the small JUCE patch in cmake/PatchJuceTouch.cmake; no window hook).
        Turns on JUCE's own touch input for that window (one source per finger,
        a press the moment a finger lands), which the gesture needs. */
    void attachTo (juce::Component& window);

    /** False where the JUCE patch isn't in (not Windows, or the patch didn't apply). */
    static bool isAvailable() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace encore
