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

/** An interface with any number of channels, some of them switched off. */
class MultiChannelDevice final : public juce::AudioIODevice
{
public:
    MultiChannelDevice (int ins, int outs, juce::BigInteger activeIns = {}, juce::BigInteger activeOuts = {})
        : juce::AudioIODevice ("Multi", "Test"), numIns (ins), numOuts (outs),
          activeIn (activeIns.isZero() ? allOf (ins) : activeIns),
          activeOut (activeOuts.isZero() ? allOf (outs) : activeOuts) {}

    static juce::BigInteger allOf (int n) { juce::BigInteger b; b.setRange (0, n, true); return b; }

    juce::StringArray getOutputChannelNames() override { return names ("Output", numOuts); }
    juce::StringArray getInputChannelNames() override  { return names ("Input", numIns); }
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
    juce::BigInteger getActiveOutputChannels() const override { return activeOut; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIn; }
    int getOutputLatencyInSamples() override                 { return 0; }
    int getInputLatencyInSamples() override                  { return 0; }

private:
    static juce::StringArray names (const juce::String& base, int n)
    {
        juce::StringArray s;
        for (int i = 0; i < n; ++i)
            s.add (base + " " + juce::String (i + 1));
        return s;
    }

    int numIns, numOuts;
    juce::BigInteger activeIn, activeOut;
};

} // namespace encore::test
