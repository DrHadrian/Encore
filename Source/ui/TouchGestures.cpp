// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/TouchGestures.h"

#if JUCE_WINDOWS && ENCORE_JUCE_TOUCH_FILTER
namespace juce
{
    // Added to JUCE's Windows window code by cmake/PatchJuceTouch.cmake: JUCE asks
    // it first for each touch message; true means Encore took the message.
    using WindowsTouchFilter = bool (*) (Component& window, int phase, unsigned int pointerId, Point<float> physicalScreenPosition);
    extern WindowsTouchFilter windowsTouchFilter;
}
#endif

namespace encore
{

//==============================================================================
void TwoFingerTapDetector::reset() noexcept
{
    first = {};
    second = {};
    gesture = false;
    spoiled = false;
}

TwoFingerTapDetector::Action TwoFingerTapDetector::down (int pointerId, juce::Point<float> at, juce::uint32 timeMs, bool allowed)
{
    if (! first.down && ! gesture)
    {
        reset();
        first = { pointerId, at, timeMs, true, false };
        return Action::pass;
    }

    if (first.down && ! gesture && ! second.down && allowed && ! first.moved && timeMs - first.downAt <= secondFingerMs)
    {
        second = { pointerId, at, timeMs, true, false };
        gesture = true;
        return Action::cancelFirstAndSwallow;
    }

    // A late second finger, or a third: ordinary multi-touch, and no right click.
    if (gesture)
        spoiled = true;

    return Action::pass;
}

TwoFingerTapDetector::Action TwoFingerTapDetector::move (int pointerId, juce::Point<float> at)
{
    for (auto* finger : { &first, &second })
        if (finger->down && finger->id == pointerId && finger->start.getDistanceFrom (at) > maxMove)
            finger->moved = true;

    if (gesture && ((first.down && pointerId == first.id) || (second.down && pointerId == second.id)))
        return Action::swallow;

    return Action::pass;
}

TwoFingerTapDetector::Action TwoFingerTapDetector::up (int pointerId, juce::Point<float> at, juce::uint32 timeMs, bool& rightClick)
{
    rightClick = false;
    move (pointerId, at);

    if (! gesture)
    {
        if (first.down && pointerId == first.id)
            reset();

        return Action::pass;
    }

    auto action = Action::pass;   // the first finger's lift reaches the app: its press is already cancelled

    if (first.down && pointerId == first.id)
        first.down = false;
    else if (second.down && pointerId == second.id)
    {
        second.down = false;
        action = Action::swallow;   // the app never saw this finger
    }
    else
    {
        return Action::pass;        // another finger
    }

    if (! first.down && ! second.down)
    {
        rightClick = ! spoiled && ! first.moved && ! second.moved && timeMs - first.downAt <= maxTapMs;
        const auto keep = first.start;
        reset();
        first.start = keep;          // getFirstPosition() for the right click
    }

    return action;
}

//==============================================================================
#if JUCE_WINDOWS && ENCORE_JUCE_TOUCH_FILTER

struct TwoFingerRightClick::Impl
{
    explicit Impl (std::function<bool()> a) : allowed (std::move (a))
    {
        active = this;
        juce::windowsTouchFilter = &Impl::filter;
    }

    ~Impl()
    {
        if (active == this)
        {
            juce::windowsTouchFilter = nullptr;
            active = nullptr;
        }
    }

    void attachTo (juce::Component& w) { window = &w; }

    /** JUCE's window code asks this first, for each touch message (phase 0 down, 1 move, 2 up). */
    static bool filter (juce::Component& w, int phase, unsigned int pointerId, juce::Point<float> at)
    {
        auto* self = active;

        if (self == nullptr || self->window.getComponent() != &w)
            return false;   // another window (a plugin's): untouched

        auto& detector = self->detector;
        const auto id = (int) pointerId;
        const auto now = juce::Time::getMillisecondCounter();
        using A = TwoFingerTapDetector::Action;

        if (phase == 0)
        {
            const auto action = detector.down (id, at, now, self->allowed == nullptr || self->allowed());

            if (action == A::cancelFirstAndSwallow)
                self->cancelFirstPress();

            return action != A::pass;
        }

        if (phase == 1)
            return detector.move (id, at) == A::swallow;

        bool rightClick = false;
        const auto taken = detector.up (id, at, now, rightClick) == A::swallow;

        if (rightClick)
        {
            // After JUCE has finished with this message.
            const auto where = detector.getFirstPosition();
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (self->window), where]
            {
                if (safe != nullptr)
                    rightClickAt (*safe, where);
            });
        }

        return taken;
    }

    /** The first finger already pressed something: move that press off the
        window and lift it there, so nothing it touched clicks. Its moves are
        taken from now on (one would press again); its lift reaches JUCE as a hover. */
    void cancelFirstPress()
    {
        for (auto& source : juce::Desktop::getInstance().getMouseSources())
        {
            if (source.getType() != juce::MouseInputSource::InputSourceType::touch || ! source.isDragging())
                continue;

            auto* peer = window != nullptr ? window->getPeer() : nullptr;

            if (peer == nullptr)
                return;

            using S = juce::MouseInputSource;
            const auto time = juce::Time::currentTimeMillis();
            const auto released = juce::ModifierKeys::getCurrentModifiers().withoutMouseButtons();
            const auto pressed = released.withFlags (juce::ModifierKeys::leftButtonModifier);
            peer->handleMouseEvent (S::InputSourceType::touch, S::offscreenMousePos, pressed, S::defaultPressure, S::defaultOrientation,
                                    time, {}, source.getIndex());
            peer->handleMouseEvent (S::InputSourceType::touch, S::offscreenMousePos, released, S::defaultPressure, S::defaultOrientation,
                                    time, {}, source.getIndex());
            juce::ModifierKeys::currentModifiers = released;
            return;   // only one finger was down
        }
    }

    /** A mouse right click at a physical screen point, as if the mouse had made it. */
    static void rightClickAt (juce::Component& w, juce::Point<float> physical)
    {
        auto* peer = w.getPeer();

        if (peer == nullptr)
            return;

        using S = juce::MouseInputSource;
        const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical (physical);
        const auto local = peer->globalToLocal (logical);
        const auto time = juce::Time::currentTimeMillis();
        const auto none = juce::ModifierKeys::getCurrentModifiers().withoutMouseButtons();
        const auto right = none.withFlags (juce::ModifierKeys::rightButtonModifier);

        juce::ModifierKeys::currentModifiers = right;
        peer->handleMouseEvent (S::InputSourceType::mouse, local, right, S::defaultPressure, S::defaultOrientation, time);
        juce::ModifierKeys::currentModifiers = none;
        peer->handleMouseEvent (S::InputSourceType::mouse, local, none, S::defaultPressure, S::defaultOrientation, time);
    }

    static inline Impl* active = nullptr;
    std::function<bool()> allowed;
    TwoFingerTapDetector detector;
    juce::Component::SafePointer<juce::Component> window;
};

#else

struct TwoFingerRightClick::Impl
{
    explicit Impl (std::function<bool()>) {}
    void attachTo (juce::Component&) {}
};

#endif

TwoFingerRightClick::TwoFingerRightClick (std::function<bool()> allowed) : impl (std::make_unique<Impl> (std::move (allowed))) {}
TwoFingerRightClick::~TwoFingerRightClick() = default;
void TwoFingerRightClick::attachTo (juce::Component& window) { impl->attachTo (window); }

} // namespace encore
