// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_events/juce_events.h>
#include <functional>

namespace encore
{

/** Runs fn on the message thread and waits for it (on the message thread it
    just runs it). For work that must happen there, like creating and
    preparing VST3 plugins, asked for by a loader thread.

    Returns false, without fn ever running, if the wait is given up:
    keepWaiting returned false (e.g. the show was closed) or Encore is
    quitting. Never call it from the audio thread. */
bool runOnMessageThread (std::function<void()> fn, std::function<bool()> keepWaiting = {});

} // namespace encore
