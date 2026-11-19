// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::theme::focusRings
{

/** Whether keyboard-focus rings are drawn right now, as CSS `:focus-visible`
    decides: only when focus last moved by the keyboard (Tab, the arrow keys, a
    shortcut). A click anywhere hides them until the keyboard moves focus
    again, so a button that gets focus because a dialog opened (its default
    button, for Enter) or got it back when one closed shows no stray ring.
    Focus itself is unchanged: Enter and Space still work.

    Message thread only. The first call starts watching (a global mouse
    listener and a focus listener on the Desktop), let go at JUCE's shutdown. */
bool visible();

/** Tests: show (or hide) the rings as if the keyboard (or a click) had moved focus last. */
void setVisibleForTests (bool shouldShow);

} // namespace encore::theme::focusRings
