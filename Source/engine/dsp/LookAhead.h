// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/dsp/FadingDelay.h"

namespace encore::dsp
{

/** A gate's look-ahead (the Encore Engine's DSP; B2): the audio path is
    delayed by a few milliseconds while the detector listens to the
    undelayed key, so the gate is already open when the pick attack arrives.

    Sized in prepare() for maxSeconds (5 ms) at the highest sample rate
    Encore runs at, so changing the rate never needs a bigger line. The time
    is set in samples; changing it while playing crossfades (FadingDelay),
    never jumps. The block reports getTimeSamples() as its latency.

    Threads: prepare() off the audio thread. Everything else on the audio
    thread: no allocation, no locks.
*/
class LookAhead
{
public:
    static constexpr double maxSeconds = 0.005;
    static constexpr double highestSampleRate = 192000.0;
    static constexpr int maxSamples = 960;   // maxSeconds at highestSampleRate

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        delay.prepare (maxSamples, sampleRate);
    }

    /** Samples for a time in ms at the prepared rate. */
    int samplesFor (double milliseconds) const noexcept
    {
        return juce::jlimit (0, maxSamples, juce::roundToInt (milliseconds * 0.001 * sampleRate));
    }

    void setTimeSamples (int samples) noexcept     { delay.setDelay (samples); }
    void setTimeSamplesNow (int samples) noexcept  { delay.setDelayNow (samples); }
    int getTimeSamples() const noexcept            { return delay.getDelay(); }
    bool isFading() const noexcept                 { return delay.isFading(); }

    void reset() noexcept                          { delay.reset(); }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        delay.process (channels, numChannels, numSamples);
    }

private:
    FadingDelay delay;
    double sampleRate = 48000.0;
};

} // namespace encore::dsp
