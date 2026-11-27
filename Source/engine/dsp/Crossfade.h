// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace encore::dsp
{

/** A true crossfade from a module's old setting to its new one (the Encore
    Engine's DSP; native modules, decision 0062).

    A choice parameter (a filter shape, the Quality switch) jumps from one
    step to the next, which would click if the sound jumped with it. Instead
    the module keeps running its old setting next to the new one for 10 ms
    and blends them: no dip, no click. Only the module knows how to run both
    (a copy of its DSP state with the old setting), so this helper only owns
    the scratch buffer for the old path and the blend.

    Use, per audio block:
        if (choice changed && ! fade.isActive())
            { oldState = state; state.set (new); fade.start(); }
        if (fade.isActive())
            { fade.copyInput (ch, n, len); oldState.process (fade.oldChannels(), n, len); }
        state.process (ch, n, len);
        if (fade.isActive())
            fade.mix (ch, n, len);

    A change that arrives during a fade waits until the fade has finished
    (at most 10 ms): the check above sees it on the next block.

    The blend is linear (equal amplitude): both paths carry the same input
    through similar filters, so they are correlated and a linear fade keeps
    the level constant.

    Threads: prepare() off the audio thread (it allocates). Everything else
    on the audio thread: no allocation, no locks.
*/
class Crossfade
{
public:
    static constexpr double defaultSeconds = 0.01;

    /** Sizes the old path's buffer. Off the audio thread. */
    void prepare (int maxChannels, int maxBlockSize, double sampleRate, double seconds = defaultSeconds)
    {
        scratch.setSize (juce::jmax (1, maxChannels), juce::jmax (1, maxBlockSize), false, true, false);
        totalSamples = juce::jmax (1, juce::roundToInt (seconds * sampleRate));
        reset();
    }

    /** No fade running. */
    void reset() noexcept               { position = totalSamples; }

    /** Starts a fade from the old path (0) to the new path (1). */
    void start() noexcept               { position = 0; }

    bool isActive() const noexcept      { return position < totalSamples; }
    int getLengthSamples() const noexcept { return totalSamples; }

    /** The old path's channels: process the old setting into these. */
    float* const* oldChannels() noexcept { return scratch.getArrayOfWritePointers(); }

    /** Copies the input into the old path before either path processes it. */
    void copyInput (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        jassert (numChannels <= scratch.getNumChannels() && numSamples <= scratch.getNumSamples());

        for (int ch = 0; ch < numChannels; ++ch)
            scratch.copyFrom (ch, 0, channels[ch], numSamples);
    }

    /** Blends the old path into newOut in place: newOut = old + (new - old) * g,
        with g rising linearly from 0 to 1 over the fade. Past the end of the
        fade the new path passes unchanged. */
    void mix (float* const* newOut, int numChannels, int numSamples) noexcept
    {
        const auto startPosition = position;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* old = scratch.getReadPointer (ch);
            auto* out = newOut[ch];
            auto p = startPosition;

            for (int i = 0; i < numSamples && p < totalSamples; ++i, ++p)
            {
                const auto g = (float) (p + 1) / (float) totalSamples;
                out[i] = old[i] + (out[i] - old[i]) * g;
            }
        }

        position = juce::jmin (totalSamples, startPosition + numSamples);
    }

private:
    juce::AudioBuffer<float> scratch;
    int totalSamples = 1, position = 1;
};

} // namespace encore::dsp
