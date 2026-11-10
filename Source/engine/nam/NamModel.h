// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <vector>

#include "engine/core/Block.h"
#include "engine/dsp/StreamResampler.h"

namespace nam { class DSP; }

namespace encore
{

/** One loaded Neural Amp Modeler capture, ready to play at the device rate.

    Everything expensive happens in load()/prepare(), off the audio thread:
    parsing the .nam file, building the network, prewarming it, measuring its
    CPU cost and setting up sample-rate conversion. process() then only runs
    the network (NAM's processing is allocation-free once prepared).

    One instance per amp block: networks have internal state, so two blocks
    must never share one.
*/
class NamModel
{
public:
    ~NamModel();

    /** Loads and prepares a model. Any thread except the audio thread.
        Returns null and sets error on failure. */
    static std::unique_ptr<NamModel> load (const juce::File& file, const ProcessSpec& spec, juce::String& error);

    /** Re-prepares for a new device rate/buffer size. Off the audio thread, not while playing. */
    void prepare (const ProcessSpec& spec);
    const ProcessSpec& getSpec() const noexcept { return spec; }

    /** Audio thread. Mono, numSamples <= spec.maxBlockSize; input and output may be the same buffer. */
    void process (const float* input, float* output, int numSamples) noexcept;

    /** Clears the network's memory (prewarms again). Off the audio thread. */
    void reset();

    //== Info ==================================================================
    const juce::File& getFile() const noexcept { return file; }
    juce::String getName() const { return file.getFileNameWithoutExtension(); }

    /** The rate the capture was made at (48 kHz assumed when the file doesn't say). */
    double getModelRate() const noexcept { return modelRate; }
    bool isResampling() const noexcept { return resampling; }

    /** Model loudness in dB, if the file records it (used for level matching). */
    std::optional<double> getLoudnessDb() const noexcept { return loudnessDb; }

    /** Measured CPU cost on this machine: share of real time needed (0.1 = 10 %). */
    double getMeasuredCpuLoad() const noexcept { return measuredCpuLoad; }

    int getLatencySamples() const noexcept { return resampling ? bridge.getLatencySamples() : 0; }

private:
    NamModel() = default;
    void runNetwork (float* samples, int numSamples) noexcept;
    void measureCpuLoad();

    juce::File file;
    std::unique_ptr<nam::DSP> dsp;
    ProcessSpec spec;
    double modelRate = 48000.0;
    bool resampling = false;
    bool arbitraryRate = false;
    std::optional<double> loudnessDb;
    double measuredCpuLoad = 0.0;

    dsp::RateBridge bridge;
    std::vector<float> networkIn, networkOut, workBuffer;
    int maxNetworkBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamModel)
};

} // namespace encore
