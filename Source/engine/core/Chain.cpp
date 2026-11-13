// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Chain.h"
#include "engine/blocks/SplitBlocks.h"

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

Block& Chain::addBlock (std::unique_ptr<Block> block, bool toPathB)
{
    jassert (block != nullptr);

    if (toPathB)
    {
        blocks.push_back (std::move (block));
    }
    else
    {
        jassert (numMain == (int) blocks.size());   // the main lane is added first
        blocks.insert (blocks.begin() + numMain, std::move (block));
        ++numMain;
    }

    auto& added = toPathB ? *blocks.back() : *blocks[(size_t) numMain - 1];

    if (preparedSpec.isValid())
        added.prepare (preparedSpec);

    return added;
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

    // Split paths: one split in the main lane, a merge after it (the session
    // builds them so; anything else plays as a plain chain).
    splitAt = mergeAt = -1;
    merge = nullptr;

    for (int i = 0; i < numMain; ++i)
    {
        const auto& type = blocks[(size_t) i]->getTypeId();

        if (splitAt < 0 && type == SplitBlock::typeId)
            splitAt = i;
        else if (splitAt >= 0 && mergeAt < 0 && type == MergeBlock::typeId)
            mergeAt = i;
    }

    if (mergeAt < 0)
        splitAt = -1;
    else
        merge = dynamic_cast<MergeBlock*> (blocks[(size_t) mergeAt].get());

    if (merge == nullptr)
        splitAt = mergeAt = -1;

    // Channels into and out of each block.
    channelsIn.assign (blocks.size(), 1);
    channelsOut.assign (blocks.size(), 1);

    auto run = [this] (int from, int to, int channels)
    {
        for (int i = from; i < to; ++i)
        {
            channelsIn[(size_t) i] = channels;
            channels = juce::jlimit (1, Block::maxChannels, blocks[(size_t) i]->getOutputChannels (channels));
            channelsOut[(size_t) i] = channels;
        }

        return channels;
    };

    auto sumLatency = [this] (int from, int to)
    {
        int total = 0;

        for (int i = from; i < to; ++i)
            total += blocks[(size_t) i]->getLatencySamples();

        return total;
    };

    if (splitAt < 0)
    {
        outputChannels = run (0, numMain, 1);
        latencyA = latencyB = 0;
        alignmentLimited = false;
    }
    else
    {
        const auto atSplit = run (0, splitAt, 1);
        channelsIn[(size_t) splitAt] = channelsOut[(size_t) splitAt] = atSplit;
        run (splitAt + 1, mergeAt, atSplit);
        run (numMain, (int) blocks.size(), atSplit);
        channelsIn[(size_t) mergeAt] = atSplit;
        channelsOut[(size_t) mergeAt] = 2;
        outputChannels = run (mergeAt + 1, numMain, 2);

        latencyA = sumLatency (splitAt + 1, mergeAt);
        latencyB = sumLatency (numMain, (int) blocks.size());
        const auto difference = std::abs (latencyB - latencyA);
        alignmentLimited = difference > maxAlignmentSamples;
        const auto align = juce::jmin (difference, maxAlignmentSamples);
        alignA.prepare (latencyB > latencyA ? align : 0, spec.maxBlockSize);
        alignB.prepare (latencyA > latencyB ? align : 0, spec.maxBlockSize);
    }

    pathB.setSize (Block::maxChannels, spec.maxBlockSize, false, true, false);
    pathBGain.assign ((size_t) spec.maxBlockSize, 1.0f);
    pathBFade.reset (spec.sampleRate, pathBFadeSeconds);
    pathBFade.setCurrentAndTargetValue (splitAt >= 0 && blocks[(size_t) splitAt]->isEnabled() ? 1.0f : 0.0f);
    pathBAsleep = false;

    // Tails: the first block that rings on. One inside the paths means the
    // paths (from the split) have to keep running.
    firstTailBlock = -1;

    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        if (! blocks[(size_t) i]->hasTail())
            continue;

        const auto inPaths = splitAt >= 0 && ((i > splitAt && i <= mergeAt) || i >= numMain);
        const auto from = inPaths ? splitAt : i;
        firstTailBlock = firstTailBlock < 0 ? from : juce::jmin (firstTailBlock, from);
    }

    if (snapshotValues == nullptr)
        saveSnapshot();
}

void Chain::Alignment::prepare (int samples, int maxBlockSize)
{
    delay = juce::jmax (0, samples);

    for (auto& line : lines)
        line.assign (delay > 0 ? (size_t) (delay + maxBlockSize + 1) : 0, 0.0f);

    writeIndex = 0;
}

void Chain::Alignment::reset() noexcept
{
    for (auto& line : lines)
        std::fill (line.begin(), line.end(), 0.0f);

    writeIndex = 0;
}

void Chain::Alignment::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (delay <= 0)
        return;

    const auto size = (int) lines[0].size();

    for (int i = 0; i < numSamples; ++i)
    {
        auto readIndex = writeIndex - delay;

        if (readIndex < 0)
            readIndex += size;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = lines[(size_t) ch];
            line[(size_t) writeIndex] = channels[ch][i];
            channels[ch][i] = line[(size_t) readIndex];
        }

        writeIndex = writeIndex + 1 < size ? writeIndex + 1 : 0;
    }
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

int Chain::processRange (int from, int to, float* const* channels, int numChannels, int numSamples,
                         const ProcessContext& context) noexcept
{
    for (int i = from; i < to; ++i)
    {
        const auto wanted = channelsOut[(size_t) i];

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
        blocks[(size_t) i]->process (channels, numChannels, numSamples, context);
    }

    return numChannels;
}

int Chain::processFrom (int firstBlock, float* const* channels, int numSamples) noexcept
{
    jassert (numSamples <= chainInput.getNumSamples());

    if (channelsIn.size() != blocks.size() || numMain == 0)
        return 1;

    // Keep the dry guitar for blocks that listen to it (e.g. a gate after the amp).
    chainInput.copyFrom (0, 0, channels[0], numSamples);
    const ProcessContext context { chainInput.getReadPointer (0) };

    auto first = juce::jlimit (0, numMain, firstBlock);

    if (splitAt >= 0 && first > splitAt && first <= mergeAt)
        first = splitAt;   // inside the paths: the paths run whole

    if (first >= numMain)
        return channelsOut[(size_t) numMain - 1];

    // Starting part-way (tails only): the signal there is what the skipped
    // blocks would have passed on, i.e. silence on every channel.
    int numChannels = channelsIn[(size_t) first];

    if (numChannels > 1)
        juce::FloatVectorOperations::copy (channels[1], channels[0], numSamples);

    if (splitAt < 0 || first > mergeAt)
        return processRange (first, numMain, channels, numChannels, numSamples, context);

    numChannels = processRange (first, splitAt, channels, numChannels, numSamples, context);

    // The split: path B gets a copy of the signal...
    float* b[Block::maxChannels] = { pathB.getWritePointer (0), pathB.getWritePointer (1) };

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy (b[ch], channels[ch], numSamples);

    // ...path A runs in place, path B in its own buffers (or sleeps while off).
    const auto aChannels = processRange (splitAt + 1, mergeAt, channels, numChannels, numSamples, context);

    pathBFade.setTargetValue (blocks[(size_t) splitAt]->isEnabled() ? 1.0f : 0.0f);
    int bChannels = numChannels;

    if (! pathBFade.isSmoothing() && pathBFade.getTargetValue() <= 0.0f)
    {
        if (! pathBAsleep)
        {
            // Off and silent: clean it up once, then skip it.
            for (auto i = (size_t) numMain; i < blocks.size(); ++i)
                blocks[i]->reset();

            alignB.reset();
            pathBAsleep = true;
        }

        std::fill (pathBGain.begin(), pathBGain.begin() + numSamples, 0.0f);
    }
    else
    {
        pathBAsleep = false;
        bChannels = processRange (numMain, (int) blocks.size(), b, numChannels, numSamples, context);

        for (int i = 0; i < numSamples; ++i)
            pathBGain[(size_t) i] = pathBFade.getNextValue();
    }

    // Line the shorter path up with the longer one.
    alignA.process (channels, aChannels, numSamples);

    if (! pathBAsleep)
        alignB.process (b, bChannels, numSamples);

    merge->mix (channels, aChannels, b, bChannels, channels, numSamples, pathBGain.data());

    return processRange (mergeAt + 1, numMain, channels, 2, numSamples, context);
}

void Chain::reset() noexcept
{
    for (auto& b : blocks)
        b->reset();

    alignA.reset();
    alignB.reset();
}

int Chain::getLatencySamples() const noexcept
{
    int total = 0;

    for (int i = 0; i < numMain; ++i)
        if (splitAt < 0 || i < splitAt || i > mergeAt)
            total += blocks[(size_t) i]->getLatencySamples();

    return total + (splitAt >= 0 ? juce::jmax (latencyA, latencyB) : 0);
}

} // namespace encore
