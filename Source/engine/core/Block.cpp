// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Block.h"

namespace encore
{

namespace
{
    constexpr double bypassRampSeconds = 0.01;
    constexpr double cpuSmoothing = 0.1;   // 10 % per block: settles in ~0.1 s at 128 samples
}

Block::Block (juce::String typeIdToUse, juce::String displayName)
    : typeId (std::move (typeIdToUse)), name (std::move (displayName))
{
}

Block::~Block() = default;

Parameter& Block::addParameter (Parameter::Spec spec)
{
    parameters.push_back (std::make_unique<Parameter> (std::move (spec)));
    return *parameters.back();
}

Parameter* Block::findParameter (const juce::String& id) const noexcept
{
    for (auto& p : parameters)
        if (p->getSpec().id == id)
            return p.get();

    return nullptr;
}

void Block::prepare (const ProcessSpec& spec)
{
    jassert (spec.isValid());

    preparedSpec = spec;
    secondsPerSample = 1.0 / spec.sampleRate;
    dryCopy.setSize (maxChannels, spec.maxBlockSize, false, true, false);

    wetAmount.reset (spec.sampleRate, bypassRampSeconds);
    wetAmount.setCurrentAndTargetValue (isEnabled() ? 1.0f : 0.0f);
    sleeping = ! isEnabled();

    for (auto& p : parameters)
        p->prepare (spec.sampleRate);

    prepareBlock (spec);
    resetBlock();
}

void Block::reset() noexcept
{
    for (auto& p : parameters)
        p->snapToTarget();

    resetBlock();
}

void Block::process (float* const* channels, int numChannels, int numSamples,
                     const ProcessContext& context) noexcept
{
    jassert (numChannels <= maxChannels && numSamples <= dryCopy.getNumSamples());

    wetAmount.setTargetValue (isEnabled() ? 1.0f : 0.0f);

    // Fully bypassed and not fading: skip the work entirely. This is what
    // makes an "off" block cost nothing.
    if (! wetAmount.isSmoothing() && wetAmount.getTargetValue() <= 0.0f)
    {
        sleeping = true;
        cpuLoad.store (0.0, std::memory_order_relaxed);
        idleBlock (numSamples);
        return;
    }

    // Waking up: start from clean state, parameters already at their values,
    // so the fade-in doesn't carry stale filter memory or a ramp from old settings.
    if (sleeping)
    {
        for (auto& p : parameters)
            p->snapToTarget();

        wakeBlock();
        sleeping = false;
    }

    const auto start = juce::Time::getHighResolutionTicks();

    for (auto& p : parameters)
        p->beginBlock();

    if (wetAmount.isSmoothing())
    {
        for (int ch = 0; ch < numChannels; ++ch)
            dryCopy.copyFrom (ch, 0, channels[ch], numSamples);

        processBlock (channels, numChannels, numSamples, context);

        // Crossfade: out = dry + (wet - dry) * amount.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = wetAmount.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto dry = dryCopy.getSample (ch, i);
                channels[ch][i] = dry + (channels[ch][i] - dry) * amount;
            }
        }
    }
    else
    {
        processBlock (channels, numChannels, numSamples, context);
    }

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    const auto proportion = elapsed / (numSamples * secondsPerSample);
    const auto previous = cpuLoad.load (std::memory_order_relaxed);
    cpuLoad.store (previous + cpuSmoothing * (proportion - previous), std::memory_order_relaxed);
}

} // namespace encore
