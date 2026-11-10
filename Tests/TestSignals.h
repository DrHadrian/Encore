// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

#include "engine/core/Block.h"

namespace encore::test
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;
inline const ProcessSpec spec { sampleRate, blockSize };

inline std::vector<float> sine (double frequency, float amplitude, int numSamples)
{
    std::vector<float> s ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        s[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);

    return s;
}

inline std::vector<float> noise (float amplitude, int numSamples, int seed = 1)
{
    juce::Random random (seed);
    std::vector<float> s ((size_t) numSamples);

    for (auto& x : s)
        x = amplitude * (random.nextFloat() * 2.0f - 1.0f);

    return s;
}

inline float rms (const std::vector<float>& s, int start, int end)
{
    double sum = 0.0;

    for (int i = start; i < end; ++i)
        sum += (double) s[(size_t) i] * s[(size_t) i];

    return (float) std::sqrt (sum / juce::jmax (1, end - start));
}

inline float peak (const std::vector<float>& s, int start, int end)
{
    float p = 0.0f;

    for (int i = start; i < end; ++i)
        p = juce::jmax (p, std::abs (s[(size_t) i]));

    return p;
}

inline float toDb (float gain) { return juce::Decibels::gainToDecibels (gain, -200.0f); }

/** Runs a mono signal through anything with process(float* const*, int, int), block by block. */
template <typename Processor>
std::vector<float> runMono (Processor& p, std::vector<float> signal)
{
    for (int offset = 0; offset < (int) signal.size(); offset += blockSize)
    {
        float* channels[1] = { signal.data() + offset };
        p.process (channels, 1, juce::jmin (blockSize, (int) signal.size() - offset));
    }

    return signal;
}

} // namespace encore::test
