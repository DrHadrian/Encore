// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <atomic>
#include <cmath>

namespace encore
{

/** A peak level shared between the audio thread (writer) and the UI (reader).

    Real-time safe: one lock-free atomic float, no allocation. The audio thread
    pushes the loudest sample of each block; the UI takes the peak and resets it.
    If a push and a take race, at worst one block's peak is lost, which a meter
    redrawing 30 times a second never shows.
*/
class PeakMeter
{
public:
    /** Audio thread: report the peak magnitude of a block. */
    void push (float peak) noexcept
    {
        if (peak > value.load (std::memory_order_relaxed))
            value.store (peak, std::memory_order_relaxed);
    }

    /** UI thread: read the highest peak since the last call, then reset it. */
    float take() noexcept { return value.exchange (0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> value { 0.0f };
    static_assert (std::atomic<float>::is_always_lock_free);
};

} // namespace encore
