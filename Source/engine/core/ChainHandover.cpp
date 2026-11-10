// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/ChainHandover.h"

#include <algorithm>

namespace encore
{

ChainHandover::ChainHandover() = default;

ChainHandover::~ChainHandover()
{
    // The audio callback must be removed before this is destroyed.
}

void ChainHandover::publish (std::unique_ptr<Chain> chain)
{
    jassert (chain != nullptr);

    const juce::ScopedLock sl (ownedLock);

    if (spec.isValid())
        chain->prepare (spec);

    auto* raw = chain.get();
    owned.push_back (std::move (chain));
    latest = raw;

    // The queue holds 16 pending swaps; the audio thread drains it every
    // few milliseconds, so filling it would need a stalled audio thread.
    const auto queued = incoming.push (raw);
    jassert (queued);
    juce::ignoreUnused (queued);
}

void ChainHandover::collectGarbage()
{
    Chain* dead = nullptr;

    while (retired.pop (dead))
    {
        const juce::ScopedLock sl (ownedLock);

        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [dead] (const auto& c) { return c.get() == dead; }),
                     owned.end());
    }
}

Chain* ChainHandover::getLatest() const noexcept
{
    const juce::ScopedLock sl (ownedLock);
    return latest;
}

void ChainHandover::prepareAll (const ProcessSpec& newSpec)
{
    const juce::ScopedLock sl (ownedLock);
    spec = newSpec;

    for (auto& c : owned)
        c->prepare (spec);
}

int ChainHandover::getNumOwnedChains() const
{
    const juce::ScopedLock sl (ownedLock);
    return (int) owned.size();
}

Chain* ChainHandover::acquireActive() noexcept
{
    // Hand back anything a full graveyard couldn't take last time.
    while (retireBacklogSize > 0 && retired.push (retireBacklog[(size_t) retireBacklogSize - 1]))
        --retireBacklogSize;

    Chain* next = nullptr;

    while (incoming.pop (next))
    {
        if (active != nullptr && ! retired.push (active))
        {
            jassert (retireBacklogSize < queueSize);

            if (retireBacklogSize < queueSize)
                retireBacklog[(size_t) retireBacklogSize++] = active;
        }

        active = next;
    }

    return active;
}

} // namespace encore
