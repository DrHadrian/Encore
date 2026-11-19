// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/FocusRings.h"

#include "ui/theme/EncoreTheme.h"

namespace encore::theme::focusRings
{

namespace
{
    /** Watches clicks and focus changes for the whole app. */
    struct Watcher final : private juce::MouseListener,
                           private juce::FocusChangeListener,
                           private juce::DeletedAtShutdown
    {
        Watcher()
        {
            juce::Desktop::getInstance().addGlobalMouseListener (this);
            juce::Desktop::getInstance().addFocusChangeListener (this);
        }

        ~Watcher() override
        {
            juce::Desktop::getInstance().removeGlobalMouseListener (this);
            juce::Desktop::getInstance().removeFocusChangeListener (this);
            clearSingletonInstance();
        }

        bool visible = false;

        void show (bool shouldShow)
        {
            if (visible == shouldShow)
                return;

            visible = shouldShow;

            // The focused control redraws with or without its ring.
            if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
                focused->repaint();
        }

        JUCE_DECLARE_SINGLETON_INLINE (Watcher, false)

    private:
        juce::uint32 lastPointer = 0;
        juce::Component::SafePointer<juce::Component> lastFocused;

        void mouseDown (const juce::MouseEvent&) override
        {
            lastPointer = juce::Time::getMillisecondCounter();
            show (false);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            lastPointer = juce::Time::getMillisecondCounter();
        }

        // (Desktop calls this asynchronously, after the click that moved focus has been seen.)
        void globalFocusChanged (juce::Component* focused) override
        {
            const auto sincePointer = juce::Time::getMillisecondCounter() - lastPointer;
            const auto fromPointer = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()
                                  || sincePointer < (juce::uint32) state::focusFromPointerMs;

            if (lastFocused != nullptr)
                lastFocused->repaint();

            lastFocused = focused;
            show (! fromPointer);

            if (focused != nullptr)
                focused->repaint();
        }
    };
}

bool visible()
{
    JUCE_ASSERT_MESSAGE_THREAD
    return Watcher::getInstance()->visible;
}

void setVisibleForTests (bool shouldShow)
{
    Watcher::getInstance()->show (shouldShow);
}

} // namespace encore::theme::focusRings
