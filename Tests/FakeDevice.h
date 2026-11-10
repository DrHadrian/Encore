// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "TestSignals.h"

namespace encore::test
{

/** Just enough of an audio device for the engine to prepare against. */
class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice() : juce::AudioIODevice ("Fake", "Test") {}
    juce::StringArray getOutputChannelNames() override       { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override        { return { "Guitar" }; }
    juce::Array<double> getAvailableSampleRates() override   { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override      { return { blockSize }; }
    int getDefaultBufferSize() override                      { return blockSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override                                   { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override                                { return true; }
    juce::String getLastError() override                     { return {}; }
    int getCurrentBufferSizeSamples() override               { return blockSize; }
    double getCurrentSampleRate() override                   { return sampleRate; }
    int getCurrentBitDepth() override                        { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override  { return 1; }
    int getOutputLatencyInSamples() override                 { return 0; }
    int getInputLatencyInSamples() override                  { return 0; }
};

} // namespace encore::test
