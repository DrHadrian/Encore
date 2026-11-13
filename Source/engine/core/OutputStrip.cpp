// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/OutputStrip.h"
#include "engine/core/PatchPlayer.h"

#include <cmath>

namespace encore
{

void OutputStrip::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    eq.prepare (sampleRate);
    limiter.prepare (sampleRate);

    // 100 ms plus a buffer's worth, so the longest delay is always in the line.
    lineSize = (int) std::ceil (routing::maxDelayMs * 0.001 * sampleRate) + juce::jmax (1, maxBlockSize) + 2;

    for (auto& line : lines)
        line.assign ((size_t) lineSize, 0.0f);

    fadeLength = juce::jmax (1, (int) (delayCrossfadeSeconds * sampleRate));
    gain.reset (sampleRate, levelRampSeconds);
    reset();
}

void OutputStrip::reset() noexcept
{
    for (auto& line : lines)
        std::fill (line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    delayBefore = delayNow;
    fadeLeft = 0;
    eq.reset();
    limiter.reset();
    gain.setCurrentAndTargetValue (0.0f);   // fades in from silence: a new output never pops
}

void OutputStrip::setSettings (const OutputEq& newEq, float delayMs) noexcept
{
    eq.setTarget (newEq.clamped());

    const auto ms = std::isfinite (delayMs) ? juce::jlimit (0.0f, routing::maxDelayMs, delayMs) : 0.0f;
    const auto samples = juce::jlimit (0, juce::jmax (0, lineSize - 2), (int) std::lround (ms * 0.001 * sampleRate));

    if (samples != delayNow)
    {
        // From wherever it is heard now to the new time, crossfaded.
        delayBefore = fadeLeft > 0 ? delayBefore : delayNow;
        delayNow = samples;
        fadeLeft = fadeLength;
    }
}

float OutputStrip::readDelayed (int channel, int delay) const noexcept
{
    auto index = writeIndex - delay;

    if (index < 0)
        index += lineSize;

    return lines[(size_t) channel][(size_t) index];
}

bool OutputStrip::process (float* left, float* right, int numSamples, float gainTarget) noexcept
{
    float* channels[2] = { left, right };

    // Level and mute, ramped so neither clicks.
    gain.setTargetValue (std::isfinite (gainTarget) ? juce::jmax (0.0f, gainTarget) : 0.0f);

    if (gain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = gain.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
    }
    else
    {
        juce::FloatVectorOperations::multiply (left, gain.getTargetValue(), numSamples);
        juce::FloatVectorOperations::multiply (right, gain.getTargetValue(), numSamples);
    }

    if (! eq.isFlat())
        eq.process (channels, 2, numSamples);

    // Delay (to line up with a far PA). Always written, so a change crossfades.
    if (lineSize > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            lines[0][(size_t) writeIndex] = left[i];
            lines[1][(size_t) writeIndex] = right[i];

            if (fadeLeft > 0)
            {
                const auto t = (float) fadeLeft / (float) fadeLength;   // 1 -> 0: from before to now
                left[i]  = readDelayed (0, delayNow) * (1.0f - t) + readDelayed (0, delayBefore) * t;
                right[i] = readDelayed (1, delayNow) * (1.0f - t) + readDelayed (1, delayBefore) * t;
                --fadeLeft;
            }
            else if (delayNow > 0)
            {
                left[i]  = readDelayed (0, delayNow);
                right[i] = readDelayed (1, delayNow);
            }

            writeIndex = writeIndex + 1 < lineSize ? writeIndex + 1 : 0;
        }
    }

    // Last line of defence before the limiter (whose state a NaN would poison).
    if (! PatchPlayer::isFinite (left, numSamples) || ! PatchPlayer::isFinite (right, numSamples))
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);

        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);

        eq.reset();
        limiter.reset();
        badBuffers.fetch_add (1, std::memory_order_relaxed);
    }

    limiter.process (channels, 2, numSamples);

    bool clipped = false;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        float peak = 0.0f;
        auto* samples = channels[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            if (std::abs (samples[i]) > hardCeiling)
            {
                samples[i] = std::copysign (hardCeiling, samples[i]);
                clipped = true;
            }

            peak = juce::jmax (peak, std::abs (samples[i]));
        }

        meters[ch].push (peak);
    }

    return clipped;
}

} // namespace encore
