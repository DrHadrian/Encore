// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

/*  A tiny VST3 the tests host, so plugin hosting can be tested without any
    third-party plugin installed. Built only with the tests, never shipped.

    It is a gain with a few ways to misbehave, chosen by its "Mode" parameter
    while playing:
        0 normal, 1 outputs NaN, 2 takes far too long, 3 crashes (access violation).
    It reports a fixed latency and keeps some state that isn't a parameter
    (a counter of how often its state was saved), so state round trips are
    tested too. With ENCORE_TEST_PLUGIN_EDITORS=<file> it notes each editor
    it creates there (a host creating editors needlessly crashes some plugins).

    Misbehaving while being scanned (in the scanner process):
        ENCORE_TEST_PLUGIN_SCAN=crash   crashes in its constructor
        ENCORE_TEST_PLUGIN_SCAN=hang    never finishes its constructor
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

namespace
{

class EncoreTestPlugin final : public juce::AudioProcessor
{
public:
    static constexpr int latency = 32;

    EncoreTestPlugin()
        : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        const auto scan = juce::SystemStats::getEnvironmentVariable ("ENCORE_TEST_PLUGIN_SCAN", {});

        if (scan == "crash")
            crash();

        if (scan == "hang")
            for (;;)
                std::this_thread::sleep_for (std::chrono::seconds (1));

        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain",
                                                            juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
        addParameter (mode = new juce::AudioParameterInt (juce::ParameterID { "mode", 1 }, "Mode", 0, 3, 0));
        setLatencySamples (latency);
    }

    const juce::String getName() const override { return "Encore Test Plugin"; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        const auto ok = [] (const juce::AudioChannelSet& s)
        {
            return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
        };
        return ok (in) && ok (out);
    }

    void prepareToPlay (double sampleRate, int) override { secondsPerSample = 1.0 / sampleRate; }
    void releaseResources() override {}

    using AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Mono in, stereo out: copy the input to the second channel first.
        if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

        switch (mode->get())
        {
            case 1:
                buffer.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
                return;

            case 2:
                // Ten times the buffer's own duration (at least 20 ms).
                std::this_thread::sleep_for (std::chrono::duration<double> (
                    juce::jmax (0.02, 10.0 * buffer.getNumSamples() * secondsPerSample)));
                break;

            case 3:
                crash();
                break;

            default:
                break;
        }

        buffer.applyGain (gain->get());
    }

    bool hasEditor() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override
    {
        // The tests check a host doesn't create editors needlessly: one line
        // per editor in the file ENCORE_TEST_PLUGIN_EDITORS names.
        if (const auto log = juce::SystemStats::getEnvironmentVariable ("ENCORE_TEST_PLUGIN_EDITORS", {}); log.isNotEmpty())
            juce::File (log).appendText ("editor\n");

        return new juce::GenericAudioProcessorEditor (*this);
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override             { return false; }
    bool producesMidi() const override            { return false; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::ValueTree state ("EncoreTestPlugin");
        state.setProperty ("gain", gain->get(), nullptr);
        state.setProperty ("saves", ++saves, nullptr);
        juce::MemoryOutputStream out (data, false);
        state.writeToStream (out);
    }

    void setStateInformation (const void* data, int size) override
    {
        const auto state = juce::ValueTree::readFromData (data, (size_t) size);

        if (state.hasType ("EncoreTestPlugin"))
        {
            *gain = (float) state.getProperty ("gain", 1.0);
            saves = state.getProperty ("saves", 0);
        }
    }

private:
    static void crash()
    {
        // A real access violation, the way a buggy plugin crashes.
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }

    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterInt* mode = nullptr;
    double secondsPerSample = 1.0 / 48000.0;
    int saves = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EncoreTestPlugin)
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EncoreTestPlugin();
}
