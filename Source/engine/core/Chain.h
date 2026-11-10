// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <memory>
#include <vector>

#include "engine/core/Block.h"

namespace encore
{

/** An ordered list of blocks that processes a buffer in place.

    Built and prepared off the audio thread, then handed to the audio thread
    whole (see ChainHandover). Its block list never changes while it plays:
    to change the chain, build a new one and hand that over instead.

    Serial for now. Splits and parallel amps (later milestones) become a small
    graph inside this class; nothing outside it needs to change.
*/
class Chain
{
public:
    Chain() = default;

    /** Before handing over only. */
    Block& addBlock (std::unique_ptr<Block> block);

    const std::vector<std::unique_ptr<Block>>& getBlocks() const noexcept { return blocks; }
    Block* findBlock (const juce::String& typeId) const noexcept;

    /** Off the audio thread, while this chain isn't playing. */
    void prepare (const ProcessSpec& spec);
    const ProcessSpec& getSpec() const noexcept { return preparedSpec; }

    /** Audio thread. numSamples <= spec.maxBlockSize. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void reset() noexcept;

    int getLatencySamples() const noexcept;

private:
    std::vector<std::unique_ptr<Block>> blocks;
    ProcessSpec preparedSpec;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chain)
};

} // namespace encore
