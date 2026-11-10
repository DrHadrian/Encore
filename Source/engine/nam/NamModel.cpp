// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/nam/NamModel.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <algorithm>
#include <filesystem>

namespace encore
{

namespace
{
    constexpr double defaultModelRate = 48000.0;   // what NAM trains at unless told otherwise
    constexpr double benchmarkSeconds = 0.5;
}

NamModel::~NamModel() = default;

std::unique_ptr<NamModel> NamModel::load (const juce::File& fileToLoad, const ProcessSpec& specToUse, juce::String& error)
{
    error.clear();

    if (! fileToLoad.existsAsFile())
    {
        error = "File not found: " + fileToLoad.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<NamModel> model (new NamModel());
    model->file = fileToLoad;

    try
    {
        // No prewarm yet: prepare() resets and prewarms at the right rate.
        nam::DspLoadOptions options;
        options.prewarm = false;
        model->dsp = nam::get_dsp (std::filesystem::path (fileToLoad.getFullPathName().toWideCharPointer()), options);
    }
    catch (const std::exception& e)
    {
        error = "Couldn't read " + fileToLoad.getFileName() + ": " + juce::String (e.what());
        return nullptr;
    }

    if (model->dsp == nullptr)
    {
        error = "Couldn't read " + fileToLoad.getFileName();
        return nullptr;
    }

    if (model->dsp->NumInputChannels() != 1 || model->dsp->NumOutputChannels() != 1)
    {
        error = fileToLoad.getFileName() + " isn't a mono amp capture";
        return nullptr;
    }

    const auto expected = model->dsp->GetExpectedSampleRate();
    model->modelRate = expected > 0.0 ? expected : defaultModelRate;
    model->arbitraryRate = model->dsp->SupportsArbitrarySampleRate();

    if (model->dsp->HasLoudness())
        model->loudnessDb = model->dsp->GetLoudness();

    try
    {
        if (specToUse.isValid())
        {
            model->prepare (specToUse);
            model->measureCpuLoad();
        }
    }
    catch (const std::exception& e)
    {
        error = "Couldn't start " + fileToLoad.getFileName() + ": " + juce::String (e.what());
        return nullptr;
    }

    return model;
}

void NamModel::prepare (const ProcessSpec& newSpec)
{
    jassert (newSpec.isValid());
    spec = newSpec;

    // Models that adapt to any rate run directly at the device rate; the rest
    // run at their training rate inside a resampling bridge.
    resampling = ! arbitraryRate && std::abs (modelRate - spec.sampleRate) > 0.5;

    if (resampling)
    {
        bridge.prepare (spec.sampleRate, modelRate, spec.maxBlockSize);
        maxNetworkBlock = bridge.getMaxInnerBlock();
    }
    else
    {
        maxNetworkBlock = spec.maxBlockSize;
    }

    networkIn.assign ((size_t) maxNetworkBlock, 0.0f);
    networkOut.assign ((size_t) maxNetworkBlock, 0.0f);
    workBuffer.assign ((size_t) spec.maxBlockSize, 0.0f);

    reset();
}

void NamModel::reset()
{
    // Reset() sizes NAM's internal buffers (it allocates) and, with prewarm on,
    // settles the network so the first notes don't start from an odd state.
    dsp->SetPrewarmOnReset (true);
    dsp->Reset (arbitraryRate ? spec.sampleRate : modelRate, maxNetworkBlock);

    if (resampling)
        bridge.reset();
}

void NamModel::runNetwork (float* samples, int numSamples) noexcept
{
    jassert (numSamples <= maxNetworkBlock);

    std::copy (samples, samples + numSamples, networkIn.data());
    float* in = networkIn.data();
    float* out = networkOut.data();
    dsp->process (&in, &out, numSamples);
    std::copy (out, out + numSamples, samples);
}

void NamModel::process (const float* input, float* output, int numSamples) noexcept
{
    jassert (numSamples <= spec.maxBlockSize);

    if (input != output)
        std::copy (input, input + numSamples, output);

    if (resampling)
        bridge.process (output, numSamples, [this] (float* s, int n) { runNetwork (s, n); });
    else
        runNetwork (output, numSamples);
}

void NamModel::measureCpuLoad()
{
    // Run half a second of quiet noise and time it, so the UI can show what
    // this capture costs on this machine before it ever plays.
    juce::Random random (42);
    std::vector<float> block ((size_t) spec.maxBlockSize);
    const auto totalSamples = (int) (benchmarkSeconds * spec.sampleRate);

    const auto start = juce::Time::getHighResolutionTicks();

    for (int done = 0; done < totalSamples; done += spec.maxBlockSize)
    {
        for (auto& x : block)
            x = 0.01f * (random.nextFloat() - 0.5f);

        process (block.data(), block.data(), spec.maxBlockSize);
    }

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    measuredCpuLoad = elapsed / benchmarkSeconds;

    reset();   // leave no trace of the benchmark in the network's memory
}

} // namespace encore
