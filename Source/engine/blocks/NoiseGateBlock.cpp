// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/NoiseGateBlock.h"

namespace encore
{

namespace
{
    constexpr double detectorDecayMs = 10.0;   // how fast the level detector falls after a peak
    constexpr double attackMs = 0.5;           // fast enough to keep the pick attack
    constexpr double holdMs = 20.0;            // stay open briefly after the level drops
    constexpr float hysteresisDb = 6.0f;

    float coefficientFor (double milliseconds, double sampleRate) noexcept
    {
        return (float) std::exp (-1.0 / (milliseconds * 0.001 * sampleRate));
    }
}

NoiseGateBlock::NoiseGateBlock()
    : Block (typeId, "Gate"),
      threshold (addParameter ({ "threshold", "Threshold", { -90.0f, 0.0f },   -60.0f, "dB", 1 })),
      release   (addParameter ({ "release",   "Release",   { 5.0f, 500.0f },    80.0f, "ms", 0 })),
      range     (addParameter ({ "range",     "Range",     { -90.0f, 0.0f },   -90.0f, "dB", 0 })),
      detectGuitar (addParameter ({ "detectGuitar", "Detect guitar", { 0.0f, 1.0f, 1.0f }, 1.0f, {}, 0 }))
{
}

void NoiseGateBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    envelopeDecay = coefficientFor (detectorDecayMs, sampleRate);
    attackCoefficient = coefficientFor (attackMs, sampleRate);
    holdSamplesTotal = (int) (holdMs * 0.001 * sampleRate);
}

void NoiseGateBlock::resetBlock() noexcept
{
    envelope = 0.0f;
    gain = 1.0f;          // start open: a block switched on mid-note must not mute it
    holdCounter = holdSamplesTotal;
    isGateOpen = true;
    open.store (true, std::memory_order_relaxed);
}

void NoiseGateBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                   const ProcessContext& context) noexcept
{
    // Listen to the clean guitar if asked (and available), else to our own input.
    const float* key = (detectGuitar.skip (numSamples) >= 0.5f) ? context.chainInput : nullptr;

    // Threshold, release and range change rarely and don't need per-sample
    // ramps: the gain itself is smoothed below, so jumping them is click-free.
    const auto openLevel = juce::Decibels::decibelsToGain (threshold.skip (numSamples));
    const auto closeLevel = openLevel * juce::Decibels::decibelsToGain (-hysteresisDb);
    const auto closedGain = juce::Decibels::decibelsToGain (range.skip (numSamples), -90.0f);
    const auto releaseCoefficient = coefficientFor ((double) release.skip (numSamples), sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;

        if (key != nullptr)
            peak = std::abs (key[i]);
        else
            for (int ch = 0; ch < numChannels; ++ch)
                peak = juce::jmax (peak, std::abs (channels[ch][i]));

        envelope = juce::jmax (peak, envelope * envelopeDecay);

        if (envelope >= openLevel)
        {
            isGateOpen = true;
            holdCounter = holdSamplesTotal;
        }
        else if (isGateOpen && envelope < closeLevel)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                isGateOpen = false;
        }

        const auto target = isGateOpen ? 1.0f : closedGain;
        const auto coefficient = target > gain ? attackCoefficient : releaseCoefficient;
        gain = target + (gain - target) * coefficient;

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }

    open.store (isGateOpen, std::memory_order_relaxed);
}

} // namespace encore
