// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/AudioEngine.h"
#include "engine/core/Realtime.h"
#include "platform/SystemTuning.h"

namespace encore
{

namespace
{
    constexpr double gainRampSeconds = 0.02;   // 20 ms: fast enough to feel instant, slow enough not to click
}

AudioEngine::AudioEngine()
{
    for (auto& g : outputLevelGain)
        g.store (1.0f, std::memory_order_relaxed);

    for (auto& m : outputMuted)
        m.store (false, std::memory_order_relaxed);

    inputActiveIndex.fill (-1);
    outputActiveIndex.fill (-1);

    // "Mute output" footswitches toggle these, by the output's name.
    midi.setOutputMutes (&outputMuted);
    midi.setOutputResolver ([this] (const juce::String& name) { return getOutputSlot (name); });

    // Until told otherwise: the guitar on input 1, everything to every output.
    setRouting (makeDefaultRouting());
}

//==============================================================================
Chain* AudioEngine::setChain (std::unique_ptr<Chain> chain)
{
    player.release (singleChain);
    singleChain = player.adopt (std::move (chain));
    player.publish ({});
    player.play (singleChain);
    return singleChain;
}

void AudioEngine::collectGarbage()
{
    player.collectGarbage();
    midi.collectGarbage();
    routingTables.collectGarbage();
}

//==============================================================================
namespace
{
    /** Gives each name a slot: a name keeps the slot it had (so its state and
        the patches' sends stay with it), new names take free slots. Returns
        the slot of each item; fresh[slot] = the slot now holds a new name. */
    template <typename List, size_t N>
    std::array<int, N> assignSlots (std::array<juce::String, N>& slotNames, const List& list, std::array<bool, N>& fresh)
    {
        std::array<int, N> slotOf;
        slotOf.fill (-1);
        std::array<juce::String, N> next;
        fresh.fill (false);

        for (size_t i = 0; i < list.size() && i < N; ++i)
            for (size_t s = 0; s < N; ++s)
                if (next[s].isEmpty() && slotNames[s].isNotEmpty() && routing::sameName (slotNames[s], list[i].name))
                {
                    next[s] = list[i].name;
                    slotOf[i] = (int) s;
                    break;
                }

        for (size_t i = 0; i < list.size() && i < N; ++i)
        {
            if (slotOf[i] >= 0)
                continue;

            for (size_t s = 0; s < N; ++s)
            {
                // The first free slot; whatever it held before is gone.
                if (next[s].isEmpty())
                {
                    next[s] = list[i].name;
                    slotOf[i] = (int) s;
                    fresh[s] = true;
                    break;
                }
            }
        }

        slotNames = next;
        return slotOf;
    }
}

void AudioEngine::setRouting (const RoutingConfig& config)
{
    routingConfig = config.sanitised();

    std::array<bool, (size_t) routing::maxInputs> freshInputs {};
    std::array<bool, (size_t) routing::maxOutputs> freshOutputs {};
    const auto inputSlots = assignSlots (inputSlotNames, routingConfig.inputs, freshInputs);
    const auto outputSlots = assignSlots (outputSlotNames, routingConfig.outputs, freshOutputs);

    auto table = std::make_unique<RoutingTable>();

    for (size_t i = 0; i < routingConfig.inputs.size(); ++i)
        table->inputs[(size_t) table->numInputs++] = { inputSlots[i], routingConfig.inputs[i].channel };

    for (size_t i = 0; i < routingConfig.outputs.size(); ++i)
    {
        const auto& out = routingConfig.outputs[i];
        const auto slot = outputSlots[i];
        RoutingTable::Output o;
        o.slot = slot;
        o.numChannels = (int) out.channels.size();

        for (size_t c = 0; c < out.channels.size() && c < 2; ++c)
            o.channels[c] = out.channels[c];

        o.eq = out.eq;
        o.delayMs = out.delayMs;
        o.fresh = freshOutputs[(size_t) slot];
        table->outputs[(size_t) table->numOutputs++] = o;

        outputLevelGain[(size_t) slot].store (routing::gainFor (out.levelDb), std::memory_order_relaxed);
        outputMuted[(size_t) slot].store (out.mute, std::memory_order_relaxed);
    }

    routingTables.publish (std::move (table));
    midi.refreshOutputs();   // "Mute output" bindings follow the names to their slots
}

bool AudioEngine::syncOutputMutes()
{
    bool changed = false;

    for (auto& out : routingConfig.outputs)
    {
        const auto slot = getOutputSlot (out.name);
        const auto now = isOutputMuted (slot);

        if (slot >= 0 && now != out.mute)
        {
            out.mute = now;
            changed = true;
        }
    }

    return changed;
}

int AudioEngine::getInputSlot (const juce::String& inputName) const noexcept
{
    const auto& input = routingConfig.inputFor (inputName);

    for (size_t s = 0; s < inputSlotNames.size(); ++s)
        if (inputSlotNames[s].isNotEmpty() && routing::sameName (inputSlotNames[s], input.name))
            return (int) s;

    return 0;
}

int AudioEngine::getOutputSlot (const juce::String& outputName) const noexcept
{
    if (outputName.trim().isEmpty())
        return -1;

    for (size_t s = 0; s < outputSlotNames.size(); ++s)
        if (outputSlotNames[s].isNotEmpty() && routing::sameName (outputSlotNames[s], outputName))
            return (int) s;

    return -1;
}

void AudioEngine::applyPatchRouting (Chain& chain, const juce::String& inputName,
                                     const std::function<float (const juce::String&)>& sendGainFor) const
{
    chain.setInputSlot (getInputSlot (inputName));

    for (size_t s = 0; s < outputSlotNames.size(); ++s)
    {
        const auto& name = outputSlotNames[s];
        const auto g = name.isEmpty() ? 0.0f : (sendGainFor != nullptr ? sendGainFor (name) : 1.0f);
        chain.setSendGain ((int) s, g);
    }
}

void AudioEngine::setOutputMuted (int slot, bool shouldBeMuted) noexcept
{
    if (juce::isPositiveAndBelow (slot, routing::maxOutputs))
        outputMuted[(size_t) slot].store (shouldBeMuted, std::memory_order_relaxed);
}

bool AudioEngine::isOutputMuted (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, routing::maxOutputs) && outputMuted[(size_t) slot].load (std::memory_order_relaxed);
}

void AudioEngine::setOutputLevel (int slot, float dB) noexcept
{
    if (juce::isPositiveAndBelow (slot, routing::maxOutputs))
        outputLevelGain[(size_t) slot].store (routing::gainFor (dB), std::memory_order_relaxed);
}

bool AudioEngine::isInputChannelUsable (int channel) const noexcept
{
    return juce::isPositiveAndNotGreaterThan (channel, routing::maxChannel)
           && ((usableInputs.load (std::memory_order_relaxed) >> (channel - 1)) & 1u) != 0;
}

bool AudioEngine::isOutputChannelUsable (int channel) const noexcept
{
    return juce::isPositiveAndNotGreaterThan (channel, routing::maxChannel)
           && ((usableOutputs.load (std::memory_order_relaxed) >> (channel - 1)) & 1u) != 0;
}

PeakMeter& AudioEngine::getInputSlotMeter (int slot) noexcept
{
    return inputSlotMeters[(size_t) juce::jlimit (0, routing::maxInputs - 1, slot)];
}

PeakMeter& AudioEngine::getOutputSlotMeter (int slot, int channel) noexcept
{
    return strips[(size_t) juce::jlimit (0, routing::maxOutputs - 1, slot)].getMeter (channel);
}

float AudioEngine::takeOutputLimiterReductionDb (int slot) noexcept
{
    return juce::isPositiveAndBelow (slot, routing::maxOutputs) ? strips[(size_t) slot].takeLimiterReductionDb() : 0.0f;
}

float AudioEngine::takeLimiterReductionDb() noexcept
{
    float deepest = 0.0f;

    for (auto& strip : strips)
        deepest = juce::jmin (deepest, strip.takeLimiterReductionDb());

    return deepest;
}

int AudioEngine::getBadOutputBufferCount() const noexcept
{
    int total = 0;

    for (auto& strip : strips)
        total += strip.getBadBufferCount();

    return total;
}

void AudioEngine::setMasterGainDecibels (float dB) noexcept
{
    masterGainDb.store (juce::jlimit (-60.0f, 12.0f, dB), std::memory_order_relaxed);
}

void AudioEngine::setMuted (bool shouldBeMuted) noexcept
{
    muted.store (shouldBeMuted, std::memory_order_relaxed);
}

void AudioEngine::resetCounters() noexcept
{
    loadMeasurer.reset (currentSampleRate.load(), currentBlockSize.load());
    clipCount.store (0, std::memory_order_relaxed);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();

    currentSampleRate.store (sampleRate);
    currentBlockSize.store (blockSize);
    loadMeasurer.reset (sampleRate, blockSize);

    // The interface's channels: which exist, which are switched on, and where
    // each switched-on one sits in the callback's channel list.
    const auto activeIn = device->getActiveInputChannels();
    const auto activeOut = device->getActiveOutputChannels();
    deviceInputs.store (device->getInputChannelNames().size(), std::memory_order_relaxed);
    deviceOutputs.store (device->getOutputChannelNames().size(), std::memory_order_relaxed);

    auto mapChannels = [] (const juce::BigInteger& active, std::array<int, (size_t) routing::maxChannel>& index)
    {
        juce::uint64 mask = 0;
        int next = 0;
        index.fill (-1);

        for (int ch = 0; ch <= active.getHighestBit(); ++ch)
        {
            if (! active[ch])
                continue;

            if (ch < routing::maxChannel)
            {
                index[(size_t) ch] = next;
                mask |= (juce::uint64) 1 << ch;
            }

            ++next;
        }

        return mask;
    };

    usableInputs.store (mapChannels (activeIn, inputActiveIndex), std::memory_order_relaxed);
    usableOutputs.store (mapChannels (activeOut, outputActiveIndex), std::memory_order_relaxed);

    // Everything the callback will touch is sized and prepared here, never in it.
    buses.setSize (2 * routing::maxOutputs, blockSize, false, true, false);
    sharedGain.assign ((size_t) blockSize, 0.0f);
    player.prepareAll ({ sampleRate, blockSize });

    for (auto& strip : strips)
        strip.prepare (sampleRate, blockSize);

    tuner.prepare (sampleRate);

    // Start the ramp from silence so a (re)started device never pops.
    gain.reset (sampleRate, gainRampSeconds);
    gain.setCurrentAndTargetValue (0.0f);
}

void AudioEngine::audioDeviceStopped()
{
    currentSampleRate.store (0.0);
    currentBlockSize.store (0);
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // AUDIO THREAD: no allocation, no locks, no I/O, no logging below this line.
    system::ensureAudioThreadIsProAudio();   // first call per thread only; then a flag check
    const rt::ScopedRealtimeSection realtime;
    const juce::ScopedNoDenormals noDenormals;
    const juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    callbacks.fetch_add (1, std::memory_order_relaxed);   // heartbeat for the device watchdog

    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    // Foot controller first: a switch pressed now takes effect in this buffer.
    midi.process (player, muted);
    handlePanicAndTunerRequests();
    adoptRoutingTable();

    if (routingTable == nullptr || buses.getNumSamples() == 0)
    {
        clearOutputs (outputChannelData, numOutputChannels, numSamples);
        return;
    }

    const auto tunerSilences = tuner.isActive() && tunerMutes.load (std::memory_order_relaxed);
    const auto target = muted.load (std::memory_order_relaxed) || tunerSilences
                            ? 0.0f
                            : juce::Decibels::decibelsToGain (masterGainDb.load (std::memory_order_relaxed));
    gain.setTargetValue (target);

    // Drivers may occasionally deliver more samples than the buffer size they
    // announced; process in chunks that fit the prepared size.
    const auto chunkSize = buses.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
        renderChunk (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, offset,
                     juce::jmin (chunkSize, numSamples - offset));
}

void AudioEngine::adoptRoutingTable() noexcept
{
    while (auto* newer = routingTables.takeNext())
    {
        routingTables.retire (routingTable);
        routingTable = newer;

        for (int i = 0; i < newer->numOutputs; ++i)
        {
            const auto& out = newer->outputs[(size_t) i];
            auto& strip = strips[(size_t) out.slot];

            if (out.fresh)
                strip.reset();

            strip.setSettings (out.eq, out.delayMs);
        }
    }
}

void AudioEngine::renderChunk (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                               int offset, int numSamples) noexcept
{
    const auto& table = *routingTable;
    PatchPlayer::Io io;

    // This computer's inputs, from the interface's channels (a channel that
    // isn't there gives silence).
    for (int i = 0; i < table.numInputs; ++i)
    {
        const auto& in = table.inputs[(size_t) i];
        const auto ch = in.channel - 1;
        const auto index = juce::isPositiveAndBelow (ch, routing::maxChannel) ? inputActiveIndex[(size_t) ch] : -1;

        if (juce::isPositiveAndBelow (index, numInputs) && inputs[index] != nullptr)
        {
            const auto* samples = inputs[index] + offset;
            io.inputs[(size_t) in.slot] = samples;
            const auto range = juce::FloatVectorOperations::findMinAndMax (samples, numSamples);
            inputSlotMeters[(size_t) in.slot].push (juce::jmax (range.getEnd(), -range.getStart()));
        }
    }

    // A bus (left, right) per output.
    for (int o = 0; o < table.numOutputs; ++o)
    {
        const auto slot = (size_t) table.outputs[(size_t) o].slot;
        io.left[slot] = buses.getWritePointer ((int) slot * 2);
        io.right[slot] = buses.getWritePointer ((int) slot * 2 + 1);
        juce::FloatVectorOperations::clear (io.left[slot], numSamples);
        juce::FloatVectorOperations::clear (io.right[slot], numSamples);
    }

    // The active patch, plus any patches still ringing out, into the buses.
    player.process (io, numSamples);

    // The tuner and the input meter follow the playing patch's input.
    const auto* playing = player.getActiveChainNow();
    const auto listenSlot = playing != nullptr ? playing->getInputSlot()
                                               : (table.numInputs > 0 ? table.inputs[0].slot : 0);

    if (const auto* input = player.getProcessedInput (listenSlot))
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (input, numSamples);
        inputMeter.push (juce::jmax (range.getEnd(), -range.getStart()));
        tuner.push (input, numSamples);   // no-op while the tuner is closed
    }

    // Master gain (with mute and tuner), ramped, times the panic fade: shared by every output.
    for (int i = 0; i < numSamples; ++i)
    {
        auto g = gain.getNextValue();

        if (panicMoving)
            panicGain = juce::jlimit (0.0f, 1.0f, panicGain + panicStep);

        sharedGain[(size_t) i] = g * panicGain;
    }

    advancePanic (numSamples);

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch] + offset, numSamples);

    float outPeak = 0.0f;
    bool clipped = false;

    auto addTo = [&] (int channel, const float* samples, float scale)
    {
        const auto ch = channel - 1;
        const auto index = juce::isPositiveAndBelow (ch, routing::maxChannel) ? outputActiveIndex[(size_t) ch] : -1;

        if (juce::isPositiveAndBelow (index, numOutputs) && outputs[index] != nullptr)
            juce::FloatVectorOperations::addWithMultiply (outputs[index] + offset, samples, scale, numSamples);
    };

    for (int o = 0; o < table.numOutputs; ++o)
    {
        const auto& out = table.outputs[(size_t) o];
        const auto slot = (size_t) out.slot;
        auto* left = io.left[slot];
        auto* right = io.right[slot];

        juce::FloatVectorOperations::multiply (left, sharedGain.data(), numSamples);
        juce::FloatVectorOperations::multiply (right, sharedGain.data(), numSamples);

        const auto level = outputMuted[slot].load (std::memory_order_relaxed)
                               ? 0.0f : outputLevelGain[slot].load (std::memory_order_relaxed);
        clipped = strips[slot].process (left, right, numSamples, level) || clipped;

        for (const auto* samples : { left, right })
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (samples, numSamples);
            outPeak = juce::jmax (outPeak, range.getEnd(), -range.getStart());
        }

        // Onto the interface: two outputs on one channel add up.
        if (out.numChannels == 0)
        {
            // All outputs (the default "Main"): the first gets left, every other right.
            for (int index = 0; index < numOutputs; ++index)
                if (outputs[index] != nullptr)
                    juce::FloatVectorOperations::add (outputs[index] + offset, index == 0 ? left : right, numSamples);
        }
        else if (out.numChannels == 1)
        {
            addTo (out.channels[0], left, 0.5f);
            addTo (out.channels[0], right, 0.5f);
        }
        else
        {
            addTo (out.channels[0], left, 1.0f);
            addTo (out.channels[1], right, 1.0f);
        }
    }

    outputMeter.push (outPeak);

    if (clipped)
        clipCount.fetch_add (1, std::memory_order_relaxed);
}

//==============================================================================
void AudioEngine::handlePanicAndTunerRequests() noexcept
{
    // Footswitches for the tuner toggle it, once per press.
    const auto tunerPresses = midi.getTunerPresses();

    if (((tunerPresses - seenMidiTuners) & 1) != 0)
        tuner.setActive (! tuner.isActive());

    seenMidiTuners = tunerPresses;

    // Panic presses, from the screen or a footswitch.
    const auto requests = panicRequests.load (std::memory_order_relaxed);
    const auto midiPanics = midi.getPanicPresses();

    if (requests == seenPanicRequests && midiPanics == seenMidiPanics)
        return;

    seenPanicRequests = requests;
    seenMidiPanics = midiPanics;

    const auto sampleRate = juce::jmax (1.0, currentSampleRate.load (std::memory_order_relaxed));
    const auto state = panicState.load (std::memory_order_relaxed);

    if (state == PanicState::silent && panicMode.load (std::memory_order_relaxed) == PanicMode::latch)
    {
        // Second press: come back.
        panicStep = (float) (1.0 / (panicReturnSeconds * sampleRate));
        panicMoving = true;
        panicState.store (PanicState::returning, std::memory_order_relaxed);
        return;
    }

    if (state == PanicState::none || state == PanicState::returning)
    {
        panicStep = -(float) (1.0 / (panicFadeSeconds * sampleRate));
        panicMoving = true;
        panicState.store (PanicState::silencing, std::memory_order_relaxed);
    }
    else if (state == PanicState::silent)
    {
        panicHeld = 0;   // recover mode, pressed again: stay silent a little longer
    }
}

void AudioEngine::advancePanic (int numSamples) noexcept
{
    const auto state = panicState.load (std::memory_order_relaxed);

    if (state == PanicState::silencing && panicGain <= 0.0f)
    {
        // Out is silent: now it's safe to cut every tail and clear the delays
        // (the outputs' delay lines too, or they would replay what was cut).
        player.panicNow();

        for (auto& strip : strips)
            strip.reset();
        panicStep = 0.0f;
        panicMoving = false;
        panicHeld = 0;
        panicState.store (PanicState::silent, std::memory_order_relaxed);
    }
    else if (state == PanicState::silent && panicMode.load (std::memory_order_relaxed) == PanicMode::recover)
    {
        panicHeld += numSamples;

        if (panicHeld >= (int) (panicHoldSeconds * juce::jmax (1.0, currentSampleRate.load (std::memory_order_relaxed))))
        {
            panicStep = (float) (1.0 / (panicReturnSeconds * currentSampleRate.load (std::memory_order_relaxed)));
            panicMoving = true;
            panicState.store (PanicState::returning, std::memory_order_relaxed);
        }
    }
    else if (state == PanicState::returning && panicGain >= 1.0f)
    {
        panicStep = 0.0f;
        panicMoving = false;
        panicState.store (PanicState::none, std::memory_order_relaxed);
    }
}

void AudioEngine::clearOutputs (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
}

} // namespace encore
