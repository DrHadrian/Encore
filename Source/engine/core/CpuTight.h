// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

namespace encore
{

/** "CPU is tight" (B4, EQ-PLAN §2.6, Proposed): the audio callback took more
    than 85 % of its buffer's time for 8 buffers in a row, or overran (more
    than 100 %). AudioEngine feeds it each callback's measured proportion
    (the one it already gives the load meter) at the end of the callback and
    passes the result to every block in the next one (ProcessContext::cpuTight).
    A block that offers "Make it lighter" steps down; the rest ignore it.

    Plain state: audio thread, no allocation.
*/
class CpuTightRule
{
public:
    static constexpr double loadLimit = 0.85;
    static constexpr int buffersInARow = 8;

    /** One callback's time over its buffer's time; returns whether it's tight now. */
    bool update (double proportion) noexcept
    {
        overInARow = proportion > loadLimit ? overInARow + 1 : 0;
        tight = proportion > 1.0 || overInARow >= buffersInARow;
        return tight;
    }

    bool isTight() const noexcept { return tight; }
    void reset() noexcept         { overInARow = 0; tight = false; }

private:
    int overInARow = 0;
    bool tight = false;
};

} // namespace encore
