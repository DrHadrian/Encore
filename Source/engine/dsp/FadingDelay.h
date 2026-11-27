// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace encore::dsp
{

/** A whole-sample delay whose time can change while it plays without a
    click (the Encore Engine's DSP).

    Used where a delay lines one signal up with another: a gate's look-ahead
    (LookAhead.h), and the Chain lining split paths up when a block's latency
    changes while playing (decision 0053; B2). A new time doesn't jump:
    the old and the new read positions play side by side and are blended
    with the smoothstep of dsp::Crossfade over the fade time. A change that
    arrives during a fade waits until the fade is over.

    The line is always written, even at a delay of 0, so a later change has
    the history it needs and fades from real signal, not from silence.

    Threads: prepare() off the audio thread (it allocates). setDelay(),
    process() and reset() on the audio thread: no allocation, no locks.
*/
class FadingDelay
{
public:
    static constexpr int maxChannels = 2;
    static constexpr double defaultFadeSeconds = 0.03;

    /** Sizes the line for up to maxDelaySamples. Off the audio thread. */
    void prepare (int maxDelaySamples, double sampleRate, double fadeSeconds = defaultFadeSeconds)
    {
        maxDelay = juce::jmax (0, maxDelaySamples);
        size = maxDelay + 1;

        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);

        fadeSamples = juce::jmax (1, juce::roundToInt (fadeSeconds * sampleRate));
        current = target = juce::jlimit (0, maxDelay, target);
        reset();
    }

    int getMaxDelay() const noexcept       { return maxDelay; }

    /** The time it is going to (what a latency report should say). */
    void setDelay (int samples) noexcept   { target = juce::jlimit (0, maxDelay, samples); }
    int getDelay() const noexcept          { return target; }

    /** Jumps to a time with no fade (before playing, or after a reset). */
    void setDelayNow (int samples) noexcept
    {
        target = current = juce::jlimit (0, maxDelay, samples);
        fadePosition = fadeSamples;
    }

    bool isFading() const noexcept         { return fadePosition < fadeSamples; }

    /** Clears the history and finishes any fade at the target time. */
    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);

        writeIndex = 0;
        current = target;
        fadePosition = fadeSamples;
    }

    /** In place, up to maxChannels channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (size <= 0)
            return;

        numChannels = juce::jmin (numChannels, maxChannels);

        if (! isFading() && target != current)
        {
            from = current;
            current = target;
            fadePosition = 0;
        }

        if (current == 0 && ! isFading())
        {
            write (channels, numChannels, numSamples);   // keep the history; the signal passes as it is
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto newRead = wrap (writeIndex - current);
            float g = 1.0f;
            int oldRead = 0;

            if (isFading())
            {
                const auto t = (float) (fadePosition + 1) / (float) fadeSamples;
                g = t * t * (3.0f - 2.0f * t);
                oldRead = wrap (writeIndex - from);
                ++fadePosition;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* line = lines[(size_t) ch].data();
                line[writeIndex] = channels[ch][i];
                const auto newer = line[newRead];
                channels[ch][i] = g >= 1.0f ? newer : line[oldRead] + (newer - line[oldRead]) * g;
            }

            writeIndex = writeIndex + 1 < size ? writeIndex + 1 : 0;
        }
    }

private:
    int wrap (int index) const noexcept { return index < 0 ? index + size : index; }

    void write (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                lines[(size_t) ch][(size_t) writeIndex] = channels[ch][i];

            writeIndex = writeIndex + 1 < size ? writeIndex + 1 : 0;
        }
    }

    std::array<std::vector<float>, maxChannels> lines;
    int maxDelay = 0, size = 0, writeIndex = 0;
    int current = 0, target = 0, from = 0;
    int fadeSamples = 1, fadePosition = 1;
};

} // namespace encore::dsp
