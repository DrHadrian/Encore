// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace encore::dsp
{

/** The last thing before the outputs: a brickwall peak limiter that keeps
    Encore's output under the ceiling no matter what a patch or a feedback
    squeal does.

    Zero latency (no look-ahead, so it adds nothing to the round trip): gain
    drops instantly on a peak and recovers smoothly. It is a safety net, not a
    tone tool; in normal playing it should never act. The meter shows when it does.
*/
class SafetyLimiter
{
public:
    static constexpr float ceilingDb = -1.0f;

    /** Off the audio thread, while not processing. */
    void prepare (double sampleRate) noexcept;

    /** Audio thread. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void reset() noexcept { gain = 1.0f; }

    /** UI: deepest gain reduction since the last call, in dB (0 = untouched), then resets. */
    float takeGainReductionDb() noexcept;

private:
    float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    float releaseCoefficient = 0.0f;
    float gain = 1.0f;
    std::atomic<float> minGain { 1.0f };
};

} // namespace encore::dsp
