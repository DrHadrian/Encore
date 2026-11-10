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

    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    // Foot controller first: a switch pressed now takes effect in this buffer.
    midi.process (player, muted);

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

    const auto target = muted.load (std::memory_order_relaxed)
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

    // Master gain, ramped.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
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

void AudioEngine::clearOutputs (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
}

} // namespace encore
