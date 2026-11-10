// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <vector>

namespace encore::dsp
{

/** Monophonic pitch detection with the YIN algorithm (de Cheveigné & Kawahara,
    2002). It finds the period at which the signal best matches a delayed copy
    of itself, which copes well with the strong harmonics of a guitar (it
    rarely jumps an octave, unlike peak picking in a spectrum).

    Not real-time safe (sizes its work buffer on first use): runs on the
    message thread from the tuner's copy of the input.
*/
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0.0f;   // Hz, 0 = no pitch
        float clarity = 0.0f;     // 0..1, how periodic the signal is
    };

    /** Lowest and highest pitch searched for (default: drop-tuned bass up to
        the top fret of a guitar's high E string). */
    void setRange (float minHz, float maxHz) noexcept { minFrequency = minHz; maxFrequency = maxHz; }

    /** Samples needed for one detection at this rate: the longest period searched
        plus the comparison window. */
    int getRequiredSamples (double sampleRate) const noexcept;

    Result detect (const float* samples, int numSamples, double sampleRate);

private:
    float minFrequency = 40.0f, maxFrequency = 1400.0f;
    std::vector<float> difference;
};

} // namespace encore::dsp
