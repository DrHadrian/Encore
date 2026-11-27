// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <cstdint>
#include <vector>

namespace encore
{

/** A decision one block shares with a later one in the same chain (B2:
    linked gates, GATE-PLAN §4). The leader owns it and writes it as it
    processes: the detected level and the open/closed decision per sample,
    the threshold it used, the latency of its output in the chain, and which
    callback it ran in. A follower reads it in the same callback (the
    leader runs first, Chain::isEarlier) and, if the leader didn't run in
    this one (off, asleep, skipped for tails), falls back to its own.

    Threads: prepare() off the audio thread (it allocates); written and read
    only on the audio thread, one callback at a time: no atomics needed.
*/
struct LinkBus
{
    /** Decisions delayed further than this (latency between leader and
        follower) are clamped, as Chain::maxAlignmentSamples. */
    static constexpr int maxDelaySamples = 8192;

    std::vector<float> level;          // the leader's detected level, per sample (linear)
    std::vector<std::uint8_t> open;    // 1 while the leader is open
    float thresholdDb = -60.0f;        // the threshold it decided with (Smart: its floor plus its offset)
    int latencyOut = 0;                // the chain latency of the signal its decisions line up with
    int numSamples = 0;
    std::uint64_t callback = 0;        // the chain callback it last ran in (0: never)

    void prepare (int maxBlockSize)
    {
        level.assign ((size_t) maxBlockSize, 0.0f);
        open.assign ((size_t) maxBlockSize, 1);
        callback = 0;
        numSamples = 0;
    }
};

} // namespace encore
