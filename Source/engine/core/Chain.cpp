// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Chain.h"

#include <cmath>

namespace encore
{

Chain::Chain()
{
    for (auto& g : sendGains)
        g.store (1.0f, std::memory_order_relaxed);
}

void Chain::setSendGain (int outputSlot, float gain) noexcept
{
    if (juce::isPositiveAndBelow (outputSlot, (int) sendGains.size()))
        sendGains[(size_t) outputSlot].store (std::isfinite (gain) ? juce::jlimit (0.0f, 4.0f, gain) : 0.0f,
                                              std::memory_order_relaxed);
}

float Chain::getSendGain (int outputSlot) const noexcept
{
    return juce::isPositiveAndBelow (outputSlot, (int) sendGains.size())
               ? sendGains[(size_t) outputSlot].load (std::memory_order_relaxed) : 0.0f;
}

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
    channelsIn.assign (blocks.size(), 1);
    channelsOut.assign (blocks.size(), 1);
    int channels = 1;   // the guitar comes in mono

    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        auto& block = *blocks[(size_t) i];
        block.prepare (spec);

        if (firstTailBlock < 0 && block.hasTail())
            firstTailBlock = i;

        channelsIn[(size_t) i] = channels;
        channels = juce::jlimit (1, Block::maxChannels, block.getOutputChannels (channels));
        channelsOut[(size_t) i] = channels;
    }

    outputChannels = channels;

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

void Chain::addExpressionTarget (int pedal, Parameter& parameter, float min, float max)
{
    expressionTargets.push_back ({ pedal, &parameter, min, max });
}

void Chain::applyExpression (int pedal, float position) noexcept
{
    for (auto& t : expressionTargets)
        if (t.pedal == pedal)
            t.parameter->setValue (t.min + (t.max - t.min) * position);   // ramped by the parameter
}

bool Chain::usesExpression (int pedal) const noexcept
{
    for (auto& t : expressionTargets)
        if (t.pedal == pedal)
            return true;

    return false;
}

bool Chain::toggleStomp (int number) noexcept
{
    bool found = false;

    for (auto& b : blocks)
    {
        if (b->getStomp() == number)
        {
            b->setEnabled (! b->isEnabled());
            found = true;
        }
    }

    return found;
}

void Chain::collectGarbage()
{
    for (auto& b : blocks)
        b->collectGarbage();
}

int Chain::process (float* const* channels, int numSamples) noexcept
{
    return processFrom (0, channels, numSamples);
}

int Chain::processFrom (int firstBlock, float* const* channels, int numSamples) noexcept
{
    jassert (numSamples <= chainInput.getNumSamples());

    // Keep the dry guitar for blocks that listen to it (e.g. a gate after the amp).
    chainInput.copyFrom (0, 0, channels[0], numSamples);
    const ProcessContext context { chainInput.getReadPointer (0) };

    const auto first = (size_t) juce::jmax (0, firstBlock);

    if (first >= blocks.size() || channelsIn.size() != blocks.size())
        return first > 0 && ! channelsOut.empty() ? channelsOut.back() : 1;

    // Starting part-way (tails only): the signal there is what the skipped
    // blocks would have passed on, i.e. silence on every channel.
    int numChannels = channelsIn[first];

    if (numChannels > 1)
        juce::FloatVectorOperations::copy (channels[1], channels[0], numSamples);

    for (size_t i = first; i < blocks.size(); ++i)
    {
        const auto wanted = channelsOut[i];

        // Mix up or down before the block, so it gets exactly what it puts out:
        // mono into a stereo block on both sides, stereo into an amp summed.
        if (wanted > numChannels)
        {
            juce::FloatVectorOperations::copy (channels[1], channels[0], numSamples);
        }
        else if (wanted < numChannels)
        {
            juce::FloatVectorOperations::add (channels[0], channels[1], numSamples);
            juce::FloatVectorOperations::multiply (channels[0], 0.5f, numSamples);
        }

        numChannels = wanted;
        blocks[i]->process (channels, numChannels, numSamples, context);
    }

    return numChannels;
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
