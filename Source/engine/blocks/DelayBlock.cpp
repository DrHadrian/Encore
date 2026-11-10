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
    buffer.assign ((size_t) (maxDelaySeconds * spec.sampleRate) + 4, 0.0f);
    toneCoefficient = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * feedbackToneHz / spec.sampleRate);
}

void DelayBlock::resetBlock() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    toneState = 0.0f;
}

void DelayBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                               const ProcessContext&) noexcept
{
    const auto size = (int) buffer.size();
    float* x = channels[0];   // mono for now

    for (int i = 0; i < numSamples; ++i)
    {
        // Fractional read position, linearly interpolated, so time changes glide.
        const auto delaySamples = juce::jlimit (1.0, (double) size - 2.0, time.nextValue() * 0.001 * sampleRate);
        auto readPos = writeIndex - delaySamples;

        if (readPos < 0.0)
            readPos += size;

        const auto i0 = (int) readPos;
        const auto i1 = (i0 + 1) % size;
        const auto frac = (float) (readPos - i0);
        const auto delayed = buffer[(size_t) i0] + (buffer[(size_t) i1] - buffer[(size_t) i0]) * frac;

        // Darken the repeats a little each time round.
        toneState = delayed + (toneState - delayed) * toneCoefficient;

        const auto dry = x[i];
        buffer[(size_t) writeIndex] = dry + toneState * feedback.nextValue() * 0.01f;
        writeIndex = (writeIndex + 1) % size;

        x[i] = dry + delayed * mix.nextValue() * 0.01f;
    }

    for (int ch = 1; ch < numChannels; ++ch)
        std::copy (x, x + numSamples, channels[ch]);
}

} // namespace encore
