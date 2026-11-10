// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/SafetyLimiter.h"

namespace encore::dsp
{

namespace
{
    constexpr double releaseMs = 150.0;
}

void SafetyLimiter::prepare (double sampleRate) noexcept
{
    releaseCoefficient = (float) std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));
    reset();
}

void SafetyLimiter::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    auto lowest = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (channels[ch][i]));

        // Recover smoothly towards unity; if that would put this sample over
        // the ceiling, drop to exactly what fits (instant attack).
        const auto recovered = 1.0f + (gain - 1.0f) * releaseCoefficient;
        gain = peak * recovered > ceiling ? ceiling / peak : recovered;

        lowest = juce::jmin (lowest, gain);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }

    if (lowest < minGain.load (std::memory_order_relaxed))
        minGain.store (lowest, std::memory_order_relaxed);
}

float SafetyLimiter::takeGainReductionDb() noexcept
{
    return juce::Decibels::gainToDecibels (minGain.exchange (1.0f, std::memory_order_relaxed), -60.0f);
}

} // namespace encore::dsp
