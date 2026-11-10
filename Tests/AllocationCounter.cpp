// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"

#include "engine/core/Realtime.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<int> realtimeAllocations { 0 };

    void noteAllocation() noexcept
    {
        if (encore::rt::isInRealtimeSection())
            realtimeAllocations.fetch_add (1, std::memory_order_relaxed);
    }

    void* allocate (std::size_t size)
    {
        noteAllocation();

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }

    void release (void* p) noexcept
    {
        if (p != nullptr)
            noteAllocation();

        std::free (p);
    }
}

int encore::test::realtimeAllocationCount() noexcept
{
    return realtimeAllocations.load (std::memory_order_relaxed);
}

// Replacements for the global allocation functions (test executable only).
void* operator new (std::size_t size)                       { return allocate (size); }
void* operator new[] (std::size_t size)                     { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void operator delete (void* p) noexcept                     { release (p); }
void operator delete[] (void* p) noexcept                   { release (p); }
void operator delete (void* p, std::size_t) noexcept        { release (p); }
void operator delete[] (void* p, std::size_t) noexcept      { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept   { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }
