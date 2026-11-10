// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

namespace encore::rt
{

/** Marks the current thread as running real-time audio code for its lifetime.

    The audio callback opens one of these around Encore's own processing. In
    Debug builds on Windows, any memory allocation or free made while it is
    open is counted as a violation (and breaks into the debugger if attached),
    so real-time bugs show up the first time they happen instead of as one
    rare click in a three-hour show.
*/
struct ScopedRealtimeSection
{
    ScopedRealtimeSection() noexcept;
    ~ScopedRealtimeSection() noexcept;

    ScopedRealtimeSection (const ScopedRealtimeSection&) = delete;
    ScopedRealtimeSection& operator= (const ScopedRealtimeSection&) = delete;
};

/** True while this thread is inside a ScopedRealtimeSection. */
bool isInRealtimeSection() noexcept;

/** Call once at start-up. Installs the allocation tripwire where supported
    (MSVC Debug builds); elsewhere it does nothing and returns false.
*/
bool installAllocationTripwire() noexcept;

/** True once installAllocationTripwire() has succeeded. */
bool isAllocationTripwireInstalled() noexcept;

/** Allocations/frees seen inside real-time sections since start-up. */
int getViolationCount() noexcept;

/** For allocators other than the MSVC debug heap (e.g. test builds): report a violation. */
void reportViolation() noexcept;

} // namespace encore::rt
