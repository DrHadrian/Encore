// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/ChainHandover.h"

namespace encore
{

void ChainHandover::publish (std::unique_ptr<Chain> chain)
{
    jassert (chain != nullptr);

    {
        const juce::ScopedLock sl (specLock);

        if (spec.isValid() && ! (chain->getSpec() == spec))
            chain->prepare (spec);
    }

    handover.publish (std::move (chain));
}

void ChainHandover::collectGarbage()
{
    handover.collectGarbage();

    // Blocks (e.g. amp models) have their own handovers; their garbage is
    // message-thread work too.
    handover.forEachOwned ([] (Chain& c) { c.collectGarbage(); });
}

void ChainHandover::prepareAll (const ProcessSpec& newSpec)
{
    const juce::ScopedLock sl (specLock);
    spec = newSpec;
    handover.forEachOwned ([&] (Chain& c) { c.prepare (spec); });
}

Chain* ChainHandover::acquireActive() noexcept
{
    while (auto* next = handover.takeNext())
    {
        handover.retire (active);
        active = next;
    }

    return active;
}

} // namespace encore
