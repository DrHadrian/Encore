// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace encore
{

/** A fixed-size, single-producer/single-consumer queue.

    One thread pushes, one other thread pops. Neither ever blocks or allocates,
    so either side may be the audio thread. Capacity is fixed at compile time.
*/
template <typename T, int Capacity>
class LockFreeFifo
{
public:
    /** Producer: returns false (and changes nothing) when the queue is full. */
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 + scope.blockSize2 != 1)
            return false;

        items[(size_t) (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)] = item;
        return true;
    }

    /** Consumer: returns false when the queue is empty. */
    bool pop (T& item) noexcept
    {
        const auto scope = fifo.read (1);

        if (scope.blockSize1 + scope.blockSize2 != 1)
            return false;

        item = items[(size_t) (scope.blockSize1 == 1 ? scope.startIndex1 : scope.startIndex2)];
        return true;
    }

    int getNumReady() const noexcept   { return fifo.getNumReady(); }
    int getFreeSpace() const noexcept  { return fifo.getFreeSpace(); }

private:
    // AbstractFifo keeps one slot free to tell "full" from "empty".
    juce::AbstractFifo fifo { Capacity + 1 };
    std::array<T, (size_t) Capacity + 1> items {};
};

} // namespace encore
