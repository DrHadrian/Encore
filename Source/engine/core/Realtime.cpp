// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Realtime.h"

#include <juce_core/juce_core.h>
#include <atomic>

#if JUCE_MSVC && defined (_DEBUG)
 #include <crtdbg.h>
 #define ENCORE_HAS_ALLOC_TRIPWIRE 1
#else
 #define ENCORE_HAS_ALLOC_TRIPWIRE 0
#endif

namespace encore::rt
{

namespace
{
    thread_local int realtimeDepth = 0;
    std::atomic<int> violations { 0 };
    std::atomic<bool> tripwireInstalled { false };

   #if ENCORE_HAS_ALLOC_TRIPWIRE
    // Called by the MSVC debug heap for every malloc/realloc/free.
    // Must not allocate itself, so it only bumps a counter.
    int __cdecl allocationHook (int, void*, size_t, int blockType, long, const unsigned char*, int)
    {
        if (blockType != _CRT_BLOCK && realtimeDepth > 0)
        {
            violations.fetch_add (1, std::memory_order_relaxed);

            if (juce::juce_isRunningUnderDebugger())
                __debugbreak();   // look at the call stack: something allocated on the audio thread
        }

        return 1;   // non-zero: let the allocation proceed (never block it)
    }
   #endif
}

ScopedRealtimeSection::ScopedRealtimeSection() noexcept   { ++realtimeDepth; }
ScopedRealtimeSection::~ScopedRealtimeSection() noexcept  { --realtimeDepth; }

bool isInRealtimeSection() noexcept { return realtimeDepth > 0; }

bool installAllocationTripwire() noexcept
{
   #if ENCORE_HAS_ALLOC_TRIPWIRE
    _CrtSetAllocHook (allocationHook);
    tripwireInstalled.store (true);
    return true;
   #else
    return false;
   #endif
}

bool isAllocationTripwireInstalled() noexcept { return tripwireInstalled.load(); }

int getViolationCount() noexcept { return violations.load (std::memory_order_relaxed); }

void reportViolation() noexcept { violations.fetch_add (1, std::memory_order_relaxed); }

} // namespace encore::rt
