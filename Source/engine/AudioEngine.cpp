// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/AudioEngine.h"

namespace encore
{

namespace
{
    constexpr double gainRampSeconds = 0.02;   // 20 ms: fast enough to feel instant, slow enough not to click
    constexpr float safetyCeiling = 0.999f;    // hard clamp until the real limiter arrives (milestone 1)
}

AudioEngine::AudioEngine() = default;

//==============================================================================
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
    const juce::ScopedNoDenormals noDenormals;
    const juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, numSamples);

    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    const auto inputIndex = guitarInput.load (std::memory_order_relaxed);
    const float* input = (inputIndex < numInputChannels) ? inputChannelData[inputIndex] : nullptr;

    if (input == nullptr)
    {
        clearOutputs (outputChannelData, numOutputChannels, numSamples);
        return;
    }

    // Input meter: the raw guitar signal, before any gain.
    const auto inputRange = juce::FloatVectorOperations::findMinAndMax (input, numSamples);
    inputMeter.push (juce::jmax (inputRange.getEnd(), -inputRange.getStart()));

    // Master gain, ramped.
    const auto target = muted.load (std::memory_order_relaxed)
                            ? 0.0f
                            : juce::Decibels::decibelsToGain (masterGainDb.load (std::memory_order_relaxed));
    gain.setTargetValue (target);

    float* const firstOut = outputChannelData[0];

    if (firstOut == nullptr)
        return;

    float outPeak = 0.0f;
    bool clipped = false;

    for (int i = 0; i < numSamples; ++i)
    {
        auto sample = input[i] * gain.getNextValue();

        if (std::abs (sample) > safetyCeiling)
        {
            sample = std::copysign (safetyCeiling, sample);
            clipped = true;
        }

        outPeak = juce::jmax (outPeak, std::abs (sample));
        firstOut[i] = sample;
    }

    // Mono guitar to every active output (stereo FOH etc.). Named output buses
    // replace this in milestone 11.
    for (int ch = 1; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::copy (outputChannelData[ch], firstOut, numSamples);

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
