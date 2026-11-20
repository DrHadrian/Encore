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

        /** A click: the pointer is in charge, rings go. */
        void pointerPressed()
        {
            keyboard = false;
            show (false);
        }

        /** Focus moved. By a key held down now (Tab, an arrow, a shortcut...):
            the keyboard is in charge, rings show. Moved by code (a rebuilt
            panel, a dialog's default button): whoever was in charge stays, so a
            click never ends with a ring however long its work takes. */
        void focusMoved (bool byKey)
        {
            if (byKey)
                keyboard = true;

            show (keyboard);
        }

        JUCE_DECLARE_SINGLETON_INLINE (Watcher, false)

    private:
        bool keyboard = false;
        juce::Component::SafePointer<juce::Component> lastFocused;

        void mouseDown (const juce::MouseEvent&) override
        {
            pointerPressed();
        }

        static bool focusKeyHeld()
        {
            using K = juce::KeyPress;
            static const int keys[] { K::tabKey, K::upKey, K::downKey, K::leftKey, K::rightKey, K::homeKey, K::endKey,
                                      K::pageUpKey, K::pageDownKey, K::returnKey, K::spaceKey, K::escapeKey,
                                      K::F1Key, K::F2Key, K::F3Key, K::F4Key, K::F5Key, K::F6Key, K::F7Key,
                                      K::F8Key, K::F9Key, K::F10Key, K::F11Key, K::F12Key };

            for (auto key : keys)
                if (K::isKeyCurrentlyDown (key))
                    return true;

            return false;
        }

        // (Desktop calls this asynchronously, before the key that moved focus is let go.)
        void globalFocusChanged (juce::Component* focused) override
        {
            if (lastFocused != nullptr)
                lastFocused->repaint();

            lastFocused = focused;
            focusMoved (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() && focusKeyHeld());

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
    if (shouldShow)
        Watcher::getInstance()->focusMoved (true);
    else
        Watcher::getInstance()->pointerPressed();
}

void clickForTests()                      { Watcher::getInstance()->pointerPressed(); }
void focusMovedForTests (bool byKey)      { Watcher::getInstance()->focusMoved (byKey); }

} // namespace encore::theme::focusRings
