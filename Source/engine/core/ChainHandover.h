// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <memory>
#include <vector>

#include "engine/core/Chain.h"
#include "engine/core/LockFreeFifo.h"

namespace encore
{

/** Moves chains between the message thread and the audio thread safely.

    Ownership never leaves the message thread:
      1. publish() prepares a chain, keeps it in an owned list and sends a raw
         pointer to the audio thread through a lock-free queue.
      2. On its next callback the audio thread takes the newest pointer, makes
         it active, and sends the previous one back through a second queue
         (the "graveyard").
      3. collectGarbage() on the message thread deletes whatever came back.
    So a chain is only ever destroyed once the audio thread has let go of it,
    and the audio thread never allocates or frees anything.

    Milestone 5 adds crossfades and tail spill-over on top of this.
*/
class ChainHandover
{
public:
    ChainHandover();
    ~ChainHandover();

    //== Message thread ========================================================
    /** Prepares (if the spec is known) and queues a chain to become active. */
    void publish (std::unique_ptr<Chain> chain);

    /** Deletes chains the audio thread has released. Call regularly (UI timer). */
    void collectGarbage();

    /** The chain most recently published (what the UI should show). */
    Chain* getLatest() const noexcept;

    /** Re-prepares every chain. Only while audio is stopped (device about to start). */
    void prepareAll (const ProcessSpec& spec);

    int getNumOwnedChains() const;

    //== Audio thread ==========================================================
    /** Applies any pending swap and returns the chain to play (may be null). */
    Chain* acquireActive() noexcept;

private:
    static constexpr int queueSize = 16;

    // Message-thread side. The lock only guards against prepareAll() being
    // called from a driver thread; the audio callback never takes it.
    mutable juce::CriticalSection ownedLock;
    std::vector<std::unique_ptr<Chain>> owned;
    Chain* latest = nullptr;
    ProcessSpec spec;

    LockFreeFifo<Chain*, queueSize> incoming, retired;

    // Audio-thread side.
    Chain* active = nullptr;
    std::array<Chain*, queueSize> retireBacklog {};
    int retireBacklogSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainHandover)
};

} // namespace encore
