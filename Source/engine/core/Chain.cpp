// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Chain.h"

namespace encore
{

Block& Chain::addBlock (std::unique_ptr<Block> block)
{
    jassert (block != nullptr);
    blocks.push_back (std::move (block));

    if (preparedSpec.isValid())
        blocks.back()->prepare (preparedSpec);

    return *blocks.back();
}

Block* Chain::findBlock (const juce::String& typeId) const noexcept
{
    for (auto& b : blocks)
        if (b->getTypeId() == typeId)
            return b.get();

    return nullptr;
}

void Chain::prepare (const ProcessSpec& spec)
{
    preparedSpec = spec;
    chainInput.setSize (1, spec.maxBlockSize, false, true, false);

    for (auto& b : blocks)
        b->prepare (spec);
}

void Chain::collectGarbage()
{
    for (auto& b : blocks)
        b->collectGarbage();
}

void Chain::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    jassert (numSamples <= chainInput.getNumSamples());

    // Keep the dry guitar for blocks that listen to it (e.g. a gate after the amp).
    chainInput.copyFrom (0, 0, channels[0], numSamples);
    const ProcessContext context { chainInput.getReadPointer (0) };

    for (auto& b : blocks)
        b->process (channels, numChannels, numSamples, context);
}

void Chain::reset() noexcept
{
    for (auto& b : blocks)
        b->reset();
}

int Chain::getLatencySamples() const noexcept
{
    int total = 0;

    for (auto& b : blocks)
        total += b->getLatencySamples();

    return total;
}

} // namespace encore
