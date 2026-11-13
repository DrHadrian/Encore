// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/DelayBlock.h"

namespace encore
{

namespace
{
    constexpr double feedbackToneHz = 5000.0;
}

DelayBlock::DelayBlock()
    : Block (typeId, "Delay"),
      time     (addParameter ({ "time",     "Time",     { 20.0f, 2000.0f, 1.0f, 0.5f }, 400.0f, "ms", 0, 0.15 })),
      feedback (addParameter ({ "feedback", "Feedback", { 0.0f, 95.0f },              35.0f,  "%",  0 })),
      mix      (addParameter ({ "mix",      "Mix",      { 0.0f, 100.0f },             30.0f,  "%",  0 }))
{
}

void DelayBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    for (auto& b : buffers)
        b.assign ((size_t) (maxDelaySeconds * spec.sampleRate) + 4, 0.0f);

    writeIndex = 0;
    toneState = {};
    toneCoefficient = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * feedbackToneHz / spec.sampleRate);
}

void DelayBlock::resetBlock() noexcept
{
    for (auto& b : buffers)
        std::fill (b.begin(), b.end(), 0.0f);

    writeIndex = 0;
    toneState = {};
}

void DelayBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                               const ProcessContext&) noexcept
{
    const auto size = (int) buffers[0].size();
    numChannels = juce::jlimit (1, maxChannels, numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        // Fractional read position, linearly interpolated, so time changes glide.
        const auto delaySamples = juce::jlimit (1.0, (double) size - 2.0, time.nextValue() * 0.001 * sampleRate);
        const auto feedbackGain = feedback.nextValue() * 0.01f;
        const auto mixGain = mix.nextValue() * 0.01f;
        auto readPos = writeIndex - delaySamples;

        if (readPos < 0.0)
            readPos += size;

        const auto i0 = (int) readPos;
        const auto i1 = (i0 + 1) % size;
        const auto frac = (float) (readPos - i0);

        // Each channel its own line: a stereo signal stays stereo.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = buffers[(size_t) ch];
            auto& tone = toneState[(size_t) ch];
            const auto delayed = line[(size_t) i0] + (line[(size_t) i1] - line[(size_t) i0]) * frac;

            // Darken the repeats a little each time round.
            tone = delayed + (tone - delayed) * toneCoefficient;

            const auto dry = channels[ch][i];
            line[(size_t) writeIndex] = dry + tone * feedbackGain;
            channels[ch][i] = dry + delayed * mixGain;
        }

        writeIndex = (writeIndex + 1) % size;
    }
}

} // namespace encore
