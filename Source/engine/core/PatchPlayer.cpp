// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/PatchPlayer.h"

#include <algorithm>

namespace encore
{

namespace
{
    // A patch that only played for a moment (stepping quickly through cues)
    // has nothing worth ringing out: it stops as soon as its input is faded out.
    constexpr double minimumPlayedSeconds = 0.05;
    constexpr double silenceHoldSeconds = 0.1;
}

PatchPlayer::PatchPlayer() = default;

PatchPlayer::~PatchPlayer()
{
    // The audio callback must be removed before this is destroyed.
}

//==============================================================================
Chain* PatchPlayer::adopt (std::unique_ptr<Chain> chain)
{
    jassert (chain != nullptr);
    const juce::ScopedLock sl (ownedLock);

    if (spec.isValid() && ! (chain->getSpec() == spec))
        chain->prepare (spec);

    owned.push_back (std::move (chain));
    return owned.back().get();
}

void PatchPlayer::release (Chain* chain)
{
    if (chain == nullptr)
        return;

    const juce::ScopedLock sl (ownedLock);

    // Unreachable once the audio thread uses a table newer than any that
    // could contain it, i.e. the next one published (which leaves it out).
    released.push_back ({ chain, latestGeneration + 1 });
}

int PatchPlayer::publish (std::vector<Cue> cues)
{
    const juce::ScopedLock sl (ownedLock);

    auto isReleased = [this] (const Chain* c)
    {
        return std::any_of (released.begin(), released.end(), [c] (const Released& r) { return r.chain == c; });
    };

    auto newTable = std::make_unique<Table>();
    newTable->generation = ++latestGeneration;

    for (auto& c : owned)
        if (! isReleased (c.get()))
            newTable->chains.push_back (c.get());

    // Every cue keeps its position (so cue numbers match the UI); a cue whose
    // patch isn't available just moves the position without changing the sound.
    for (auto cue : cues)
    {
        if (cue.chain != nullptr && isReleased (cue.chain))
            cue.chain = nullptr;

        newTable->cues.push_back (cue);
    }

    tables.publish (std::move (newTable));
    return latestGeneration;
}

void PatchPlayer::sendCommand (const Command& command)
{
    if (! commands.push (command))
        droppedCommands.fetch_add (1);
}

void PatchPlayer::play (Chain* chain)     { sendCommand ({ Command::Type::play, chain, 0 }); }
void PatchPlayer::next()                  { sendCommand ({ Command::Type::next, nullptr, 0 }); }
void PatchPlayer::previous()              { sendCommand ({ Command::Type::previous, nullptr, 0 }); }
void PatchPlayer::goToCue (int index)     { sendCommand ({ Command::Type::goTo, nullptr, index }); }
void PatchPlayer::setCuePosition (int index) { sendCommand ({ Command::Type::setPosition, nullptr, index }); }

void PatchPlayer::collectGarbage()
{
    tables.collectGarbage();

    const juce::ScopedLock sl (ownedLock);

    // Blocks have their own handovers (replaced amp models, IRs).
    for (auto& c : owned)
        c->collectGarbage();

    const auto audioIsAt = audioGeneration.load();

    for (auto it = released.begin(); it != released.end();)
    {
        const auto* chain = it->chain;
        const auto stillPlaying = std::any_of (inUse.begin(), inUse.end(),
                                               [chain] (const std::atomic<Chain*>& p) { return p.load() == chain; });

        if (audioIsAt >= it->generation && ! stillPlaying)
        {
            owned.erase (std::remove_if (owned.begin(), owned.end(),
                                         [chain] (const auto& o) { return o.get() == chain; }),
                         owned.end());
            it = released.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void PatchPlayer::prepareAll (const ProcessSpec& newSpec)
{
    const juce::ScopedLock sl (ownedLock);
    spec = newSpec;

    for (auto& c : owned)
        c->prepare (spec);

    // Audio is stopped: size the audio thread's buffers too.
    inputCopy.assign ((size_t) spec.maxBlockSize, 0.0f);
    slotBuffer.assign ((size_t) spec.maxBlockSize, 0.0f);
    mix.assign ((size_t) spec.maxBlockSize, 0.0f);

    sampleRate = spec.sampleRate;
    fadeStep = (float) (1.0 / juce::jmax (1.0, switchFadeSeconds * sampleRate));
    settleSamples = (int) (tailSettleSeconds * sampleRate);
    timeoutSamples = (int) (tailTimeoutSeconds * sampleRate);
    silenceHoldSamples = (int) (silenceHoldSeconds * sampleRate);
}

int PatchPlayer::getNumOwnedChains() const
{
    const juce::ScopedLock sl (ownedLock);
    return (int) owned.size();
}

//==============================================================================
bool PatchPlayer::tableContains (const Chain* chain) const noexcept
{
    if (table == nullptr || chain == nullptr)
        return false;

    // Pointer comparison only: a chain not in the current table may already be freed.
    return std::find (table->chains.begin(), table->chains.end(), chain) != table->chains.end();
}

void PatchPlayer::applyCommand (const Command& command) noexcept
{
    using Type = Command::Type;

    if (command.type == Type::play)
    {
        if (tableContains (command.chain))
            switchTo (command.chain, false);

        return;
    }

    if (command.type == Type::setPosition)
    {
        cueIndex = table != nullptr ? juce::jlimit (-1, (int) table->cues.size() - 1, command.index) : -1;
        cueForUi.store (cueIndex);
        return;
    }

    if (table == nullptr || table->cues.empty())
        return;

    const auto last = (int) table->cues.size() - 1;
    int target = cueIndex;

    switch (command.type)
    {
        case Type::next:     target = cueIndex < 0 ? 0 : juce::jmin (cueIndex + 1, last); break;
        case Type::previous: target = juce::jmax (0, cueIndex - 1); break;
        case Type::goTo:     target = juce::jlimit (0, last, command.index); break;
        case Type::play:
        case Type::setPosition: break;
    }

    if (target == cueIndex && command.type != Type::goTo)
        return;   // already at the end (or start): nothing to do

    cueIndex = target;
    cueForUi.store (cueIndex);

    const auto& cue = table->cues[(size_t) cueIndex];
    switchTo (cue.chain, cue.resetIfSamePatch);
}

void PatchPlayer::switchTo (Chain* target, bool resetIfSame) noexcept
{
    if (target == nullptr)
        return;

    if (target == active.chain)
    {
        // Same patch on the next cue: keep playing, or go back to its saved settings.
        if (resetIfSame)
            target->restoreSnapshot();

        return;
    }

    // Was it still ringing out? Bring it back, tail and all.
    Slot incoming;

    for (auto& slot : ringing)
    {
        if (slot.chain == target)
        {
            incoming = slot;
            slot = {};
            break;
        }
    }

    if (incoming.chain == nullptr)
    {
        incoming.chain = target;
        incoming.activeFor = 0;
    }

    incoming.inputTarget = 1.0f;
    incoming.outputGain = 1.0f;
    incoming.cutting = false;
    incoming.age = incoming.silentFor = 0;

    // The patch we leave starts ringing out.
    if (active.chain != nullptr)
    {
        auto freeSlot = std::find_if (ringing.begin(), ringing.end(), [] (const Slot& s) { return s.chain == nullptr; });

        if (freeSlot == ringing.end())
        {
            // All ringing slots busy (very fast stepping): stop the one that has
            // been ringing longest, which is the quietest.
            freeSlot = std::max_element (ringing.begin(), ringing.end(),
                                         [] (const Slot& a, const Slot& b) { return a.age < b.age; });
            freeSlot->chain->reset();
            *freeSlot = {};
        }

        *freeSlot = active;
        freeSlot->inputTarget = 0.0f;
        freeSlot->age = 0;
        freeSlot->silentFor = 0;
    }

    active = incoming;
    publishSlotsForUi();
}

void PatchPlayer::publishSlotsForUi() noexcept
{
    inUse[0].store (active.chain);
    int count = 0;

    for (size_t i = 0; i < ringing.size(); ++i)
    {
        inUse[i + 1].store (ringing[i].chain);
        count += ringing[i].chain != nullptr ? 1 : 0;
    }

    activeForUi.store (active.chain);
    ringingForUi.store (count);
}

//==============================================================================
void PatchPlayer::renderSlot (Slot& slot, const float* input, float* out, int numSamples, bool isRinging) noexcept
{
    float* buffer = slotBuffer.data();
    const auto inputSilent = slot.inputGain <= 0.0f && slot.inputTarget <= 0.0f;

    if (inputSilent)
    {
        std::fill (buffer, buffer + numSamples, 0.0f);
    }
    else
    {
        // Input crossfade: the leaving patch's input fades out while the new
        // one's fades in. Each chain's own decay then carries the sound across.
        for (int i = 0; i < numSamples; ++i)
        {
            if (slot.inputGain < slot.inputTarget)
                slot.inputGain = juce::jmin (slot.inputTarget, slot.inputGain + fadeStep);
            else if (slot.inputGain > slot.inputTarget)
                slot.inputGain = juce::jmax (slot.inputTarget, slot.inputGain - fadeStep);

            buffer[i] = input[i] * slot.inputGain;
        }
    }

    int firstBlock = 0;
    bool finished = false;

    if (! isRinging)
        slot.activeFor += numSamples;

    if (isRinging)
    {
        slot.age += numSamples;

        if (inputSilent)
        {
            if (slot.activeFor < (int) (minimumPlayedSeconds * sampleRate))
            {
                finished = true;   // only passed through it: nothing worth ringing out
            }
            else if (slot.chain->getTailMode() == Chain::TailMode::tailsOnly && slot.age > settleSamples)
            {
                firstBlock = slot.chain->getFirstTailBlock();
                finished = firstBlock < 0;   // nothing in this patch rings
            }
        }
    }

    if (! finished)
    {
        float* channels[1] = { buffer };
        slot.chain->processFrom (firstBlock, channels, 1, numSamples);

        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            out[i] += buffer[i] * slot.outputGain;
            peak = juce::jmax (peak, std::abs (buffer[i]));
        }

        if (isRinging && inputSilent)
        {
            slot.silentFor = peak < silenceThreshold ? slot.silentFor + numSamples : 0;
            finished = (slot.age > settleSamples && slot.silentFor >= silenceHoldSamples)
                       || slot.age >= timeoutSamples;
        }
    }

    if (finished)
    {
        slot.chain->reset();   // clean state for the next time it plays
        slot = {};
        publishSlotsForUi();
    }
}

void PatchPlayer::process (float* io, int numSamples) noexcept
{
    // Newest running order first, then commands, which refer to it.
    while (auto* newer = tables.takeNext())
    {
        tables.retire (table);
        table = newer;
        audioGeneration.store (table->generation);
    }

    Command command;

    while (commands.pop (command))
        applyCommand (command);

    if ((int) mix.size() < numSamples)
    {
        std::fill (io, io + numSamples, 0.0f);   // not prepared yet
        return;
    }

    std::copy (io, io + numSamples, inputCopy.data());
    std::fill (mix.begin(), mix.begin() + numSamples, 0.0f);

    if (active.chain != nullptr)
        renderSlot (active, inputCopy.data(), mix.data(), numSamples, false);

    for (auto& slot : ringing)
        if (slot.chain != nullptr)
            renderSlot (slot, inputCopy.data(), mix.data(), numSamples, true);

    std::copy (mix.begin(), mix.begin() + numSamples, io);
}

} // namespace encore
