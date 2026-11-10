// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Chain.h"
#include "engine/core/RealtimeHandover.h"

namespace encore
{

/** The engine's active chain: a RealtimeHandover that also keeps every chain
    prepared for the current device, and switches to the newest chain at the
    start of each callback.

    Milestone 5 adds crossfades and tail spill-over on top of this.
*/
class ChainHandover
{
public:
    ChainHandover() = default;

    //== Message thread ========================================================
    /** Prepares (if the device spec is known) and queues a chain to become active. */
    void publish (std::unique_ptr<Chain> chain);

    /** Deletes chains the audio thread has released, and anything their blocks released. */
    void collectGarbage();

    /** The chain most recently published (what the UI should show). */
    Chain* getLatest() const noexcept { return handover.getLatest(); }

    /** Re-prepares every chain. Only while audio is stopped (device about to start). */
    void prepareAll (const ProcessSpec& spec);

    int getNumOwnedChains() const { return handover.getNumOwned(); }

    //== Audio thread ==========================================================
    /** Applies any pending swap and returns the chain to play (may be null). */
    Chain* acquireActive() noexcept;

private:
    RealtimeHandover<Chain> handover;
    juce::CriticalSection specLock;
    ProcessSpec spec;

    Chain* active = nullptr;   // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainHandover)
};

} // namespace encore
