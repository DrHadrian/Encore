// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/Oversampler.h"

namespace encore::dsp
{

void Oversampler::prepare (int maxChannels, int maxBlockSize, double sampleRate)
{
    baseRate = sampleRate;
    maxChannels = juce::jlimit (1, 8, maxChannels);

    for (int q = 0; q < numQualities; ++q)
    {
        // factor is the number of 2x stages: 1, 2, 3.
        auto os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) maxChannels, (size_t) (q + 1),
                                                                     juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                                     true, true);
        os->initProcessing ((size_t) juce::jmax (1, maxBlockSize));
        latencies[(size_t) q] = juce::roundToInt (os->getLatencyInSamples());
        stages[(size_t) q] = std::move (os);
    }

    fade.prepare (maxChannels, maxBlockSize, sampleRate);
    current = previous = requested;
    latency.store (latencies[(size_t) current], std::memory_order_relaxed);
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& os : stages)
        if (os != nullptr)
            os->reset();

    fade.reset();
}

} // namespace encore::dsp
