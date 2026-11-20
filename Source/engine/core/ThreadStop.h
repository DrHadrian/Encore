// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

namespace encore
{

/** Stops a juce::Thread and waits for it, however long it takes. Never kills it.

    juce::Thread::stopThread (n) calls TerminateThread on Windows after n ms.
    A thread killed while it holds the loader lock (it does while it ends) or
    the heap lock leaves that lock held for good: every thread start, thread
    exit and DLL load after it hangs (decision 0060). So Encore never passes a
    time limit: a slow stop is logged once, and the wait goes on. */
inline void stopThreadWithoutKilling (juce::Thread& thread)
{
    thread.signalThreadShouldExit();
    thread.notify();

    if (! thread.waitForThreadToExit (5000))
    {
        juce::Logger::writeToLog ("Waiting for the thread \"" + thread.getThreadName() + "\" to stop");
        thread.waitForThreadToExit (-1);
    }
}

} // namespace encore
