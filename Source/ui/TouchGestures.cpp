// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/TouchGestures.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <windowsx.h>
 #include <commctrl.h>   // SetWindowSubclass (comctl32, linked in CMakeLists.txt)
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
#if JUCE_WINDOWS

struct TwoFingerRightClick::Impl
{
    explicit Impl (std::function<bool()> a) : allowed (std::move (a)) {}

    ~Impl()
    {
        detach();
    }

    void attachTo (juce::Component& w)
    {
        window = &w;
        auto* peer = w.getPeer();
        const auto handle = peer != nullptr ? (HWND) peer->getNativeHandle() : nullptr;

        if (handle == hwnd)
            return;

        detach();
        hwnd = handle;

        if (hwnd != nullptr)
            SetWindowSubclass (hwnd, &Impl::proc, subclassId, (DWORD_PTR) this);
    }

    void detach()
    {
        if (hwnd != nullptr && IsWindow (hwnd))
            RemoveWindowSubclass (hwnd, &Impl::proc, subclassId);

        hwnd = nullptr;
    }

    static LRESULT CALLBACK proc (HWND h, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto* self = reinterpret_cast<Impl*> (data);

        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass (h, &Impl::proc, subclassId);
            self->hwnd = nullptr;
            return DefSubclassProc (h, message, wParam, lParam);
        }

        if (message != WM_POINTERDOWN && message != WM_POINTERUPDATE && message != WM_POINTERUP)
            return DefSubclassProc (h, message, wParam, lParam);

        const auto id = (UINT32) GET_POINTERID_WPARAM (wParam);
        POINTER_INPUT_TYPE type = PT_POINTER;

        if (! GetPointerType (id, &type) || type != PT_TOUCH)
            return DefSubclassProc (h, message, wParam, lParam);

        const juce::Point<float> at ((float) GET_X_LPARAM (lParam), (float) GET_Y_LPARAM (lParam));   // physical screen pixels
        const auto now = (juce::uint32) GetTickCount();
        auto& detector = self->detector;
        using A = TwoFingerTapDetector::Action;

        if (message == WM_POINTERDOWN)
        {
            const auto action = detector.down ((int) id, at, now, self->allowed == nullptr || self->allowed());

            if (action == A::cancelFirstAndSwallow)
            {
                self->cancelFirstPress();
                return 0;
            }

            return action == A::swallow ? 0 : DefSubclassProc (h, message, wParam, lParam);
        }

        if (message == WM_POINTERUPDATE)
            return detector.move ((int) id, at) == A::swallow ? 0 : DefSubclassProc (h, message, wParam, lParam);

        bool rightClick = false;
        const auto action = detector.up ((int) id, at, now, rightClick);
        const auto result = action == A::swallow ? 0 : DefSubclassProc (h, message, wParam, lParam);

        if (rightClick)
        {
            // After the window procedure has finished with this message.
            const auto where = detector.getFirstPosition();
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (self->window), where]
            {
                if (safe != nullptr)
                    rightClickAt (*safe, where);
            });
        }

        return result;
    }

    /** The first finger already pressed something: move that press off the
        window and lift it there, so nothing it touched clicks. Its moves are
        swallowed from now on (one would press again), its lift passes as a hover. */
    void cancelFirstPress()
    {
        for (auto& source : juce::Desktop::getInstance().getMouseSources())
        {
            if (source.getType() != juce::MouseInputSource::InputSourceType::touch || ! source.isDragging())
                continue;

            auto* under = source.getComponentUnderMouse();
            auto* peer = under != nullptr ? under->getPeer() : (window != nullptr ? window->getPeer() : nullptr);

            if (peer == nullptr)
                continue;

            const auto time = juce::Time::currentTimeMillis();
            const auto pressed = juce::ModifierKeys::getCurrentModifiers().withoutMouseButtons().withFlags (juce::ModifierKeys::leftButtonModifier);
            const auto released = juce::ModifierKeys::getCurrentModifiers().withoutMouseButtons();
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::touch, juce::MouseInputSource::offscreenMousePos, pressed,
                                    juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time, {}, source.getIndex());
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::touch, juce::MouseInputSource::offscreenMousePos, released,
                                    juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time, {}, source.getIndex());
            juce::ModifierKeys::currentModifiers = released;
            break;   // only one finger was down
        }
    }

    /** A mouse right click at a physical screen point, as if the mouse had made it. */
    static void rightClickAt (juce::Component& window, juce::Point<float> physical)
    {
        auto* peer = window.getPeer();

        if (peer == nullptr)
            return;

        const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical (physical);
        const auto local = peer->globalToLocal (logical);
        const auto time = juce::Time::currentTimeMillis();
        const auto none = juce::ModifierKeys::getCurrentModifiers().withoutMouseButtons();
        const auto right = none.withFlags (juce::ModifierKeys::rightButtonModifier);
        using S = juce::MouseInputSource;

        juce::ModifierKeys::currentModifiers = right;
        peer->handleMouseEvent (S::InputSourceType::mouse, local, right, S::defaultPressure, S::defaultOrientation, time);
        juce::ModifierKeys::currentModifiers = none;
        peer->handleMouseEvent (S::InputSourceType::mouse, local, none, S::defaultPressure, S::defaultOrientation, time);
    }

    static constexpr UINT_PTR subclassId = 0x456E636F;   // "Enco"
    std::function<bool()> allowed;
    TwoFingerTapDetector detector;
    juce::Component::SafePointer<juce::Component> window;
    HWND hwnd = nullptr;
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
