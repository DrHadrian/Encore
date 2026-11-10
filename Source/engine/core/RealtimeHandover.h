// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "engine/core/LockFreeFifo.h"

namespace encore
{

/** Hands objects (chains, amp models...) from the message thread to the audio
    thread and back, without the audio thread ever allocating or freeing.

    Ownership never leaves the message thread:
      1. publish() keeps the object in an owned list and sends a raw pointer
         to the audio thread through a lock-free queue.
      2. The audio thread takes it with takeNext(); when it no longer needs an
         object it calls retire(), which sends the pointer back.
      3. collectGarbage() on the message thread deletes whatever came back.

    The audio side decides when to retire (immediately for chains, after a
    crossfade for amp models), so the policy lives with the caller.
*/
template <typename T, int QueueSize = 16>
class RealtimeHandover
{
public:
    RealtimeHandover() = default;

    //== Message thread ========================================================
    /** Takes ownership and queues the object for the audio thread. */
    T* publish (std::unique_ptr<T> object)
    {
        jassert (object != nullptr);

        const juce::ScopedLock sl (ownedLock);
        auto* raw = object.get();
        owned.push_back (std::move (object));
        latest = raw;

        // The audio thread drains this every few milliseconds; filling it
        // would need a stalled audio thread.
        const auto queued = incoming.push (raw);
        jassert (queued);
        juce::ignoreUnused (queued);
        return raw;
    }

    /** Deletes objects the audio thread has retired. Call regularly. */
    void collectGarbage()
    {
        T* dead = nullptr;

        while (retired.pop (dead))
        {
            const juce::ScopedLock sl (ownedLock);
            owned.erase (std::remove_if (owned.begin(), owned.end(),
                                         [dead] (const auto& o) { return o.get() == dead; }),
                         owned.end());

            if (latest == dead)
                latest = nullptr;
        }
    }

    /** The most recently published object that hasn't been deleted. */
    T* getLatest() const noexcept
    {
        const juce::ScopedLock sl (ownedLock);
        return latest;
    }

    /** Calls fn on every owned object. Not while the audio thread could use them,
        unless fn only touches thread-safe state. */
    template <typename Fn>
    void forEachOwned (Fn&& fn)
    {
        const juce::ScopedLock sl (ownedLock);

        for (auto& o : owned)
            fn (*o);
    }

    int getNumOwned() const
    {
        const juce::ScopedLock sl (ownedLock);
        return (int) owned.size();
    }

    //== Audio thread ==========================================================
    /** The next published object, or null. Call repeatedly to drain. */
    T* takeNext() noexcept
    {
        flushBacklog();

        T* next = nullptr;
        return incoming.pop (next) ? next : nullptr;
    }

    /** Hands an object back for deletion on the message thread. Never blocks:
        if the return queue is full it is kept and retried on the next call. */
    void retire (T* object) noexcept
    {
        if (object == nullptr)
            return;

        flushBacklog();

        if (! retired.push (object))
        {
            jassert (backlogSize < QueueSize);

            if (backlogSize < QueueSize)
                backlog[(size_t) backlogSize++] = object;
        }
    }

private:
    void flushBacklog() noexcept
    {
        while (backlogSize > 0 && retired.push (backlog[(size_t) backlogSize - 1]))
            --backlogSize;
    }

    // Message-thread side. The lock only guards against calls from driver
    // threads (e.g. a device restart); the audio thread never takes it.
    mutable juce::CriticalSection ownedLock;
    std::vector<std::unique_ptr<T>> owned;
    T* latest = nullptr;

    LockFreeFifo<T*, QueueSize> incoming, retired;

    // Audio-thread side.
    std::array<T*, (size_t) QueueSize> backlog {};
    int backlogSize = 0;

    JUCE_DECLARE_NON_COPYABLE (RealtimeHandover)
};

} // namespace encore
