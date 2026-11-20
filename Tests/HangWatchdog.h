// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <string>

namespace encore::testing
{

/** Ends a test run that has stopped making progress, and says where it stopped.

    ENCORE_TEST_HANG_SECONDS=<n> turns it on (CI sets it; off by default, so a
    test paused in a debugger is never killed). When no test step has started
    and no heartbeat has come for n seconds, it prints the step that was
    running and, on Windows, the stack of every thread, then exits with code 3.
    Without it a hang only shows as ctest's timeout, with no clue to the cause.

    Kept free of JUCE, so the Windows side can include windows.h. */
class HangWatchdog
{
public:
    /** Reads ENCORE_TEST_HANG_SECONDS; does nothing when it's unset or 0. */
    HangWatchdog();
    ~HangWatchdog();

    /** A test step started (the runner's log messages). */
    static void step (const std::string& what);

    HangWatchdog (const HangWatchdog&) = delete;
    HangWatchdog& operator= (const HangWatchdog&) = delete;

private:
    struct Impl;
    Impl* impl = nullptr;
};

/** Still working (a long test, such as the soak, calls this as it goes).
    Lock-free and allocation-free. */
void heartbeat() noexcept;

} // namespace encore::testing
