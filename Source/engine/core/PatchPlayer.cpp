// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/PatchPlayer.h"

#include <algorithm>
#include <cmath>

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
    for (auto& b : inputCopies)
        b.assign ((size_t) spec.maxBlockSize, 0.0f);

    for (auto& b : slotBuffers)
        b.assign ((size_t) spec.maxBlockSize, 0.0f);

    silence.assign ((size_t) spec.maxBlockSize, 0.0f);
    mixLeft.assign ((size_t) spec.maxBlockSize, 0.0f);
    mixRight.assign ((size_t) spec.maxBlockSize, 0.0f);

    sampleRate = spec.sampleRate;
    sendStep = (float) (1.0 / juce::jmax (1.0, sendRampSeconds * sampleRate));
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
int PatchPlayer::findCueForProgram (int program) const noexcept
{
    if (table == nullptr)
        return -1;

    for (int i = 0; i < (int) table->cues.size(); ++i)
        if (table->cues[(size_t) i].programChange == program)
            return i;

    return program < (int) table->cues.size() ? program : -1;
}

float PatchPlayer::stepFor (float seconds) const noexcept
{
    return (float) (1.0 / juce::jmax (1.0, seconds * sampleRate));
}

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
    incoming.inputStep = stepFor (target->getFadeInSeconds());
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
        freeSlot->inputStep = stepFor (active.chain->getRingOutSeconds());
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
void PatchPlayer::renderSlot (Slot& slot, const Io& io, int numSamples, bool isRinging) noexcept
{
    float* channels[Block::maxChannels] = { slotBuffers[0].data(), slotBuffers[1].data() };
    float* buffer = channels[0];
    const auto inputSilent = slot.inputGain <= 0.0f && slot.inputTarget <= 0.0f;
    const auto inputSlot = slot.chain->getInputSlot();
    const float* input = juce::isPositiveAndBelow (inputSlot, routing::maxInputs) && inputGiven[(size_t) inputSlot]
                             ? inputCopies[(size_t) inputSlot].data() : silence.data();

    if (inputSilent)
    {
        std::fill (buffer, buffer + numSamples, 0.0f);
    }
    else
    {
        // Input crossfade: the leaving patch's input fades out (its ring-out
        // time) while the new one's fades in (its fade-in time). Each chain's
        // own decay then carries the sound across.
        for (int i = 0; i < numSamples; ++i)
        {
            if (slot.inputGain < slot.inputTarget)
                slot.inputGain = juce::jmin (slot.inputTarget, slot.inputGain + slot.inputStep);
            else if (slot.inputGain > slot.inputTarget)
                slot.inputGain = juce::jmax (slot.inputTarget, slot.inputGain - slot.inputStep);

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
        const auto numChannels = juce::jlimit (1, Block::maxChannels, slot.chain->processFrom (firstBlock, channels, numSamples));
        const float* left = channels[0];
        const float* right = channels[numChannels - 1];   // mono: the same on both sides

        // A block that blew up (NaN/inf, e.g. an unstable filter) must never
        // reach the speakers: drop this buffer and give the patch a clean state.
        if (! isFinite (left, numSamples) || (numChannels > 1 && ! isFinite (right, numSamples)))
        {
            std::fill (channels[0], channels[0] + numSamples, 0.0f);
            std::fill (channels[1], channels[1] + numSamples, 0.0f);
            slot.chain->reset();
            badOutputs.fetch_add (1, std::memory_order_relaxed);
        }

        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

        // Into every output through this patch's sends, each ramped towards
        // what the chain asks for (so a send change or a switch never jumps).
        for (size_t o = 0; o < (size_t) routing::maxOutputs; ++o)
        {
            auto& current = slot.sends[o];
            const auto target = slot.chain->getSendGain ((int) o);

            if (! slot.sendsSet)
                current = target;

            float* outLeft = io.left[o];
            float* outRight = io.right[o];

            if (outLeft == nullptr || outRight == nullptr)
            {
                current = target;   // not routed now: no ramp to catch up on later
                continue;
            }

            if (juce::exactlyEqual (current, target))
            {
                const auto g = current * slot.outputGain;

                if (g > 0.0f)
                {
                    juce::FloatVectorOperations::addWithMultiply (outLeft, left, g, numSamples);
                    juce::FloatVectorOperations::addWithMultiply (outRight, right, g, numSamples);
                }

                continue;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                current = current < target ? juce::jmin (target, current + sendStep)
                                           : juce::jmax (target, current - sendStep);
                const auto g = current * slot.outputGain;
                outLeft[i] += left[i] * g;
                outRight[i] += right[i] * g;
            }
        }

        slot.sendsSet = true;

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

bool PatchPlayer::isFinite (const float* samples, int numSamples) noexcept
{
    // Summing magnitudes is fast and catches NaN and inf in one pass (a
    // finite overflow to inf is also worth treating as broken).
    float sum = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        sum += std::abs (samples[i]);

    return std::isfinite (sum);
}

void PatchPlayer::panicNow() noexcept
{
    for (auto& slot : ringing)
    {
        if (slot.chain != nullptr)
            slot.chain->reset();

        slot = {};
    }

    if (active.chain != nullptr)
        active.chain->reset();

    publishSlotsForUi();
}

void PatchPlayer::adoptLatestTable() noexcept
{
    while (auto* newer = tables.takeNext())
    {
        tables.retire (table);
        table = newer;
        audioGeneration.store (table->generation);
    }
}

void PatchPlayer::process (const Io& io, int numSamples) noexcept
{
    // Newest running order first, then commands, which refer to it.
    adoptLatestTable();

    Command command;

    while (commands.pop (command))
        applyCommand (command);

    if ((int) silence.size() < numSamples)
        return;   // not prepared yet: adds nothing

    // A driver delivering garbage is silenced rather than fed into every patch.
    for (size_t i = 0; i < (size_t) routing::maxInputs; ++i)
    {
        inputGiven[i] = io.inputs[i] != nullptr;

        if (! inputGiven[i])
            continue;

        auto* copy = inputCopies[i].data();
        std::copy (io.inputs[i], io.inputs[i] + numSamples, copy);

        if (! isFinite (copy, numSamples))
        {
            std::fill (copy, copy + numSamples, 0.0f);
            badInputs.fetch_add (1, std::memory_order_relaxed);
        }
    }

    if (active.chain != nullptr)
        renderSlot (active, io, numSamples, false);

    for (auto& slot : ringing)
        if (slot.chain != nullptr)
            renderSlot (slot, io, numSamples, true);
}

const float* PatchPlayer::getProcessedInput (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, routing::maxInputs) && inputGiven[(size_t) slot]
               ? inputCopies[(size_t) slot].data() : nullptr;
}

void PatchPlayer::process (float* io, int numSamples) noexcept
{
    if ((int) mixLeft.size() < numSamples)
    {
        process (Io {}, numSamples);             // commands still count
        std::fill (io, io + numSamples, 0.0f);   // not prepared yet
        return;
    }

    std::fill (mixLeft.begin(), mixLeft.begin() + numSamples, 0.0f);
    std::fill (mixRight.begin(), mixRight.begin() + numSamples, 0.0f);

    Io mono;
    mono.inputs[0] = io;
    mono.left[0] = mixLeft.data();
    mono.right[0] = mixRight.data();
    process (mono, numSamples);

    for (int i = 0; i < numSamples; ++i)
        io[i] = (mixLeft[(size_t) i] + mixRight[(size_t) i]) * 0.5f;
}

} // namespace encore
