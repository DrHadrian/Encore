// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/MessageThread.h"

#include <memory>

namespace encore
{

bool runOnMessageThread (std::function<void()> fn, std::function<bool()> keepWaiting)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();

    if (mm == nullptr)
        return false;

    if (mm->isThisTheMessageThread())
    {
        fn();
        return true;
    }

    struct Pending
    {
        juce::CriticalSection lock;
        juce::WaitableEvent done;
        bool finished = false, abandoned = false;
    };

    auto pending = std::make_shared<Pending>();

    const auto posted = juce::MessageManager::callAsync ([pending, fn = std::move (fn)]
    {
        const juce::ScopedLock sl (pending->lock);

        if (pending->abandoned)
            return;

        fn();
        pending->finished = true;
        pending->done.signal();
    });

    if (! posted)
        return false;

    for (;;)
    {
        if (pending->done.wait (50))
            return true;

        if (mm->hasStopMessageBeenSent() || (keepWaiting != nullptr && ! keepWaiting()))
        {
            const juce::ScopedLock sl (pending->lock);

            if (pending->finished)
                return true;

            pending->abandoned = true;
            return false;
        }
    }
}

} // namespace encore
