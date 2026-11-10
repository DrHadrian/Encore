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

    firstTailBlock = -1;

    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        blocks[(size_t) i]->prepare (spec);

        if (firstTailBlock < 0 && blocks[(size_t) i]->hasTail())
            firstTailBlock = i;
    }

    if (snapshotValues == nullptr)
        saveSnapshot();
}

void Chain::saveSnapshot()
{
    size_t count = 0;

    for (auto& b : blocks)
        count += b->getParameters().size();

    if (snapshotValues == nullptr || count != snapshotSize)
    {
        // Only before the chain plays (sizes are fixed once blocks are added).
        snapshotValues.reset (new std::atomic<float>[count]);
        snapshotEnabled.reset (new std::atomic<bool>[blocks.size()]);
        snapshotSize = count;
    }

    size_t index = 0;

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        snapshotEnabled[i].store (blocks[i]->isEnabled());

        for (auto& p : blocks[i]->getParameters())
            snapshotValues[index++].store (p->getValue());
    }
}

void Chain::restoreSnapshot() noexcept
{
    if (snapshotValues == nullptr)
        return;

    size_t index = 0;

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        blocks[i]->setEnabled (snapshotEnabled[i].load());

        for (auto& p : blocks[i]->getParameters())
            if (index < snapshotSize)
                p->setValue (snapshotValues[index++].load());
    }
}

void Chain::collectGarbage()
{
    for (auto& b : blocks)
        b->collectGarbage();
}

void Chain::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    processFrom (0, channels, numChannels, numSamples);
}

void Chain::processFrom (int firstBlock, float* const* channels, int numChannels, int numSamples) noexcept
{
    jassert (numSamples <= chainInput.getNumSamples());

    // Keep the dry guitar for blocks that listen to it (e.g. a gate after the amp).
    chainInput.copyFrom (0, 0, channels[0], numSamples);
    const ProcessContext context { chainInput.getReadPointer (0) };

    for (size_t i = (size_t) juce::jmax (0, firstBlock); i < blocks.size(); ++i)
        blocks[i]->process (channels, numChannels, numSamples, context);
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
