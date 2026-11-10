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
    constexpr float hardCeiling = 0.999f;      // last resort after the limiter; should never act
}

AudioEngine::AudioEngine() = default;

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
}

void AudioEngine::setMasterGainDecibels (float dB) noexcept
{
    masterGainDb.store (juce::jlimit (-60.0f, 12.0f, dB), std::memory_order_relaxed);
}

void AudioEngine::setMuted (bool shouldBeMuted) noexcept
{
    muted.store (shouldBeMuted, std::memory_order_relaxed);
}

void AudioEngine::setGuitarInputChannel (int activeChannelIndex) noexcept
{
    guitarInput.store (juce::jmax (0, activeChannelIndex), std::memory_order_relaxed);
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

    // Everything the callback will touch is sized and prepared here, never in it.
    work.setSize (Block::maxChannels, blockSize, false, true, false);
    player.prepareAll ({ sampleRate, blockSize });
    limiter.prepare (sampleRate);
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

    const auto inputIndex = guitarInput.load (std::memory_order_relaxed);
    const float* input = (inputIndex < numInputChannels) ? inputChannelData[inputIndex] : nullptr;

    if (input == nullptr || work.getNumSamples() == 0)
    {
        clearOutputs (outputChannelData, numOutputChannels, numSamples);
        return;
    }

    // Input meter: the raw guitar signal, before anything else.
    const auto inputRange = juce::FloatVectorOperations::findMinAndMax (input, numSamples);
    inputMeter.push (juce::jmax (inputRange.getEnd(), -inputRange.getStart()));
    tuner.push (input, numSamples);   // no-op while the tuner is closed

    const auto tunerSilences = tuner.isActive() && tunerMutes.load (std::memory_order_relaxed);
    const auto target = muted.load (std::memory_order_relaxed) || tunerSilences
                            ? 0.0f
                            : juce::Decibels::decibelsToGain (masterGainDb.load (std::memory_order_relaxed));
    gain.setTargetValue (target);

    // Drivers may occasionally deliver more samples than the buffer size they
    // announced; process in chunks that fit the prepared size.
    const auto chunkSize = work.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
        renderChunk (input, outputChannelData, numOutputChannels, offset,
                     juce::jmin (chunkSize, numSamples - offset));
}

void AudioEngine::renderChunk (const float* input, float* const* outputs, int numOutputs,
                               int offset, int numSamples) noexcept
{
    float* channels[Block::maxChannels] = { work.getWritePointer (0), work.getWritePointer (1) };
    const int numChannels = 1;   // guitar is mono until a stereo block (later milestones)

    juce::FloatVectorOperations::copy (channels[0], input + offset, numSamples);

    // The active patch, plus any patches still ringing out (mono).
    player.process (channels[0], numSamples);

    // Master gain (with mute and tuner), ramped, times the panic fade.
    for (int i = 0; i < numSamples; ++i)
    {
        auto g = gain.getNextValue();

        if (panicMoving)
            panicGain = juce::jlimit (0.0f, 1.0f, panicGain + panicStep);

        g *= panicGain;

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }

    advancePanic (numSamples);

    // Last line of defence before the limiter (whose state a NaN would poison).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (! PatchPlayer::isFinite (channels[ch], numSamples))
        {
            juce::FloatVectorOperations::clear (channels[ch], numSamples);
            limiter.reset();
        }
    }

    limiter.process (channels, numChannels, numSamples);

    float outPeak = 0.0f;
    bool clipped = false;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto& sample = channels[ch][i];

            if (std::abs (sample) > hardCeiling)
            {
                sample = std::copysign (hardCeiling, sample);
                clipped = true;
            }

            outPeak = juce::jmax (outPeak, std::abs (sample));
        }
    }

    // Mono (or stereo) result to every active output. Named output buses
    // (FOH, IEM...) replace this in milestone 11.
    for (int out = 0; out < numOutputs; ++out)
        if (outputs[out] != nullptr)
            juce::FloatVectorOperations::copy (outputs[out] + offset,
                                               channels[juce::jmin (out, numChannels - 1)],
                                               numSamples);

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
        // Out is silent: now it's safe to cut every tail and clear the delays.
        player.panicNow();
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
