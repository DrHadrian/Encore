// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/EngineProbeBlock.h"

namespace encore
{

EngineProbeBlock::EngineProbeBlock()
    : Block (typeId, "Engine probe"),
      trim (addParameter ({ "trim", "Trim", { -24.0f, 24.0f }, 0.0f, "dB", 1 }))
{
}

void EngineProbeBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                     const ProcessContext&) noexcept
{
    // At 0 dB, and not moving, nothing is touched: bit-exact passthrough.
    if (! trim.isSmoothing() && juce::exactlyEqual (trim.currentValue(), 0.0f))
    {
        trim.skip (numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = juce::Decibels::decibelsToGain (trim.nextValue());

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }
}

} // namespace encore
