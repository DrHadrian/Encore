// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Chain.h"
#include "engine/blocks/SplitBlocks.h"

#include "engine/core/LinkBus.h"

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

    if (splitAt < 0)
    {
        outputChannels = run (0, numMain, 1);
        latencyA = latencyB = 0;
        alignmentLimited = false;
        alignA.prepare (0, spec.sampleRate);
        alignB.prepare (0, spec.sampleRate);
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

        // Sized for the most that can be lined up, so a latency that changes
        // while playing only moves the delay (updateAlignment).
        alignA.prepare (maxAlignmentSamples, spec.sampleRate, alignmentFadeSeconds);
        alignB.prepare (maxAlignmentSamples, spec.sampleRate, alignmentFadeSeconds);
        updateAlignment();
        alignA.setDelayNow (alignA.getDelay());
        alignB.setDelayNow (alignB.getDelay());
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

    resolveKeys();
    resolveLinks();

    if (snapshotValues == nullptr)
        saveSnapshot();
}

Chain::Region Chain::regionOf (int index) const noexcept
{
    if (splitAt < 0)
        return Region::beforeSplit;

    if (index >= numMain)
        return Region::pathB;

    if (index <= splitAt)
        return Region::beforeSplit;

    return index < mergeAt ? Region::pathA : Region::afterMerge;
}

bool Chain::isEarlier (int earlier, int later) const noexcept
{
    const auto count = (int) blocks.size();

    if (! juce::isPositiveAndBelow (earlier, count) || ! juce::isPositiveAndBelow (later, count) || earlier == later)
        return false;

    const auto a = regionOf (earlier), b = regionOf (later);

    switch (b)
    {
        case Region::beforeSplit: return a == Region::beforeSplit && earlier < later;
        case Region::pathA:       return a == Region::beforeSplit || (a == Region::pathA && earlier < later);
        case Region::pathB:       return a == Region::beforeSplit || (a == Region::pathB && earlier < later);
        case Region::afterMerge:  return a != Region::afterMerge || earlier < later;
    }

    return false;
}

int Chain::indexOfModelId (const juce::String& modelId) const noexcept
{
    if (modelId.isEmpty())
        return -1;

    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i]->getModelId() == modelId)
            return i;

    return -1;
}

void Chain::resolveKeys()
{
    taps.clear();
    taps.resize (blocks.size());
    anyTaps = false;

    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        auto& block = *blocks[(size_t) i];
        const auto wanted = block.getRef ("keyBlock");

        if (wanted.isEmpty())
        {
            block.setKeyTap (nullptr, 0, {});
            continue;
        }

        const auto source = indexOfModelId (wanted);

        if (source < 0)
        {
            block.setKeyTap (nullptr, 0, "Key: that block isn't in this patch");
            continue;
        }

        if (! isEarlier (source, i))
        {
            block.setKeyTap (nullptr, 0, "Key: that block isn't before this gate");
            continue;
        }

        auto& tap = taps[(size_t) source];

        if (tap == nullptr)
            tap = std::make_unique<juce::AudioBuffer<float>> (Block::maxChannels, juce::jmax (1, preparedSpec.maxBlockSize));

        tap->clear();
        anyTaps = true;
    }

    // Now that every tap exists, hand them out (their data doesn't move again).
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        auto& block = *blocks[(size_t) i];
        const auto source = indexOfModelId (block.getRef ("keyBlock"));

        if (source >= 0 && isEarlier (source, i) && taps[(size_t) source] != nullptr)
            block.setKeyTap (taps[(size_t) source]->getArrayOfReadPointers(), channelsOut[(size_t) source], {});
    }
}

void Chain::resolveLinks()
{
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        auto& block = *blocks[(size_t) i];
        const auto wanted = block.getRef ("linkTo");

        if (wanted.isEmpty())
        {
            block.setLinkLeader (nullptr, {});
            continue;
        }

        const auto leader = indexOfModelId (wanted);

        if (leader < 0)
            block.setLinkLeader (nullptr, "Link: that gate isn't in this patch");
        else if (blocks[(size_t) leader]->getLinkBus() == nullptr)
            block.setLinkLeader (nullptr, "Link: that block isn't a gate");
        else if (! isEarlier (leader, i))
            block.setLinkLeader (nullptr, "Link: that gate isn't before this one");
        else
            block.setLinkLeader (blocks[(size_t) leader]->getLinkBus(), {});
    }
}

void Chain::clearTaps (int from, int to) noexcept
{
    if (! anyTaps)
        return;

    for (int i = juce::jmax (0, from); i < juce::jmin (to, (int) taps.size()); ++i)
        if (taps[(size_t) i] != nullptr)
            taps[(size_t) i]->clear();
}

static_assert (LinkBus::maxDelaySamples == Chain::maxAlignmentSamples);

int Chain::sumLatency (int from, int to) const noexcept
{
    int total = 0;

    for (int i = from; i < to; ++i)
        total += juce::jmax (0, blocks[(size_t) i]->getLatencySamples());

    return total;
}

void Chain::updateAlignment() noexcept
{
    if (splitAt < 0)
        return;

    const auto a = sumLatency (splitAt + 1, mergeAt);
    const auto b = sumLatency (numMain, (int) blocks.size());
    const auto difference = std::abs (b - a);
    const auto align = juce::jmin (difference, maxAlignmentSamples);

    latencyA.store (a, std::memory_order_relaxed);
    latencyB.store (b, std::memory_order_relaxed);
    alignmentLimited.store (difference > maxAlignmentSamples, std::memory_order_relaxed);
    alignA.setDelay (b > a ? align : 0);
    alignB.setDelay (a > b ? align : 0);
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

int Chain::process (float* const* channels, int numSamples, const ChainInputs& inputs) noexcept
{
    return processFrom (0, channels, numSamples, inputs);
}

int Chain::processRange (int from, int to, float* const* channels, int numChannels, int numSamples,
                         ProcessContext& context) noexcept
{
    // context.latencyIn: the latency so far; each block adds its own.
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
        auto& block = *blocks[(size_t) i];
        block.process (channels, numChannels, numSamples, context);
        context.latencyIn += juce::jmax (0, block.getLatencySamples());

        // A tap: a later block is keyed from this one's output (B2).
        if (anyTaps && taps[(size_t) i] != nullptr)
        {
            auto& tap = *taps[(size_t) i];

            for (int ch = 0; ch < Block::maxChannels; ++ch)
                tap.copyFrom (ch, 0, channels[juce::jmin (ch, numChannels - 1)], numSamples);
        }
    }

    return numChannels;
}

int Chain::processFrom (int firstBlock, float* const* channels, int numSamples, const ChainInputs& inputs) noexcept
{
    jassert (numSamples <= chainInput.getNumSamples());

    if (channelsIn.size() != blocks.size() || numMain == 0)
        return 1;

    // Keep the dry guitar for blocks that listen to it (e.g. a gate after the amp).
    chainInput.copyFrom (0, 0, channels[0], numSamples);
    ProcessContext context;
    context.chainInput = chainInput.getReadPointer (0);
    context.inputs = inputs.inputs;
    context.numInputs = inputs.numInputs;
    context.tunerOpen = inputs.tunerOpen;
    context.callback = ++callbackCount;

    auto first = juce::jlimit (0, numMain, firstBlock);

    if (splitAt >= 0 && first > splitAt && first <= mergeAt)
        first = splitAt;   // inside the paths: the paths run whole

    if (first >= numMain)
        return channelsOut[(size_t) numMain - 1];

    // Starting part-way (tails only): the signal there is what the skipped
    // blocks would have passed on, i.e. silence on every channel; so are
    // their taps.
    if (first > 0)
        clearTaps (0, first);

    int numChannels = channelsIn[(size_t) first];
    context.latencyIn = sumLatency (0, first);   // what the skipped blocks would have added

    if (numChannels > 1)
        juce::FloatVectorOperations::copy (channels[1], channels[0], numSamples);

    if (splitAt < 0 || first > mergeAt)
        return processRange (first, numMain, channels, numChannels, numSamples, context);

    numChannels = processRange (first, splitAt, channels, numChannels, numSamples, context);

    // The split: path B gets a copy of the signal...
    float* b[Block::maxChannels] = { pathB.getWritePointer (0), pathB.getWritePointer (1) };

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy (b[ch], channels[ch], numSamples);

    // ...path A runs in place, path B in its own buffers (or sleeps while off),
    // both starting from the latency at the split.
    const auto atSplit = context.latencyIn;
    const auto aChannels = processRange (splitAt + 1, mergeAt, channels, numChannels, numSamples, context);
    const auto afterA = context.latencyIn;
    context.latencyIn = atSplit;

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
            clearTaps (numMain, (int) blocks.size());   // asleep: it passes on nothing
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

    // Line the shorter path up with the longer one (following any latency
    // that changed in this buffer, crossfaded).
    updateAlignment();
    alignA.process (channels, aChannels, numSamples);

    if (! pathBAsleep)
        alignB.process (b, bChannels, numSamples);

    merge->mix (channels, aChannels, b, bChannels, channels, numSamples, pathBGain.data());

    // After the merge both paths are lined up with the longer one.
    context.latencyIn = atSplit + juce::jmax (afterA - atSplit, sumLatency (numMain, (int) blocks.size()));
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

    if (splitAt >= 0)
        total += juce::jmax (sumLatency (splitAt + 1, mergeAt), sumLatency (numMain, (int) blocks.size()));

    return total;
}

} // namespace encore
