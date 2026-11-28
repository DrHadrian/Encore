// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <algorithm>
#include <cmath>

#include "engine/dsp/SoftClip.h"

namespace encore::dsp
{

/** The Encore Engine's character curves (Studio EQ "Soft" and "Warm", stage
    B4; the drives will use them). Memoryless, in the oversampled domain;
    plain maths, any thread. Values Proposed, tuned on the rig in U3.

    Soft: gentle odd-order, console-like. The SoftClip curve made symmetric:
    exactly linear to -12 dBFS, 1 dB down at 0 dBFS on both sides. Symmetric,
    so it makes no even harmonics at all.

    Warm: more even-order, tape or tube-like. A smooth asymmetric curve first,
        e(x) = (1 - exp (-k x)) / k        (slope 1 at 0; k = 0.3)
    whose x^2 term gives a second harmonic that grows with the level (about
    -40 dB at -18 dBFS, -32 dB at -10 dBFS), then an asymmetric SoftClip
    (linear to -12 dBFS, 1 dB down at -2 dBFS positive, +4 dBFS negative) that
    keeps it bounded and adds a little odd-order when driven hard. The second
    harmonic is its largest from a whisper to a firm drive (Drive 18 at
    -18 dBFS); past that, as any clipper, the odd ones take over. It makes DC:
    the module puts a DC blocker after it. Its "low-end weight" is the
    module's LowEmphasis around it, so the lows push the curve a little harder.
*/
struct Saturation
{
    enum class Curve { soft, warm };

    static constexpr double softLinearDb = -12.0, softOneDbPointDb = 0.0;
    static constexpr double warmK = 0.3;
    static constexpr double warmLinearDb = -12.0, warmPositiveOneDbDb = -2.0, warmNegativeOneDbDb = 4.0;

    Saturation() noexcept
        : soft (softLinearDb, softOneDbPointDb, softOneDbPointDb),
          warmClip (warmLinearDb, warmPositiveOneDbDb, warmNegativeOneDbDb)
    {
    }

    float process (Curve curve, float x) const noexcept
    {
        if (curve == Curve::soft)
            return soft.process (x);

        // exp of more than 30 is beyond anything the clip lets through anyway.
        const auto e = (1.0 - std::exp (-warmK * std::clamp ((double) x, -30.0, 30.0))) / warmK;
        return warmClip.process ((float) e);
    }

    const SoftClip soft, warmClip;
};

/** A first-order low shelf and its exact inverse (Warm's pre- and
    de-emphasis): the lows go into the curve a little hotter and come back
    down after it, so a quiet signal comes out as it went in (to float
    precision) and only the saturation is shaped. Bilinear (corner far below
    Nyquist, no cramping to speak of). State in double.

    set(): any thread before use, or the audio thread (no allocation).
    process(): audio thread. */
class LowEmphasis
{
public:
    static constexpr double defaultFrequency = 150.0, defaultGainDb = 3.0;   // Proposed

    void set (double sampleRate, double frequency = defaultFrequency, double gainDb = defaultGainDb, bool inverse = false) noexcept
    {
        // H(s) = (s + w G) / (s + w): G at DC, 1 above the corner; the inverse swaps them.
        const auto w = 2.0 * sampleRate * std::tan (3.141592653589793 * frequency / sampleRate);
        const auto g = std::pow (10.0, gainDb / 20.0);
        const auto k = 2.0 * sampleRate;
        auto zeroW = w * g, poleW = w;

        if (inverse)
            std::swap (zeroW, poleW);

        const auto a0 = k + poleW;
        b0 = (k + zeroW) / a0;
        b1 = (zeroW - k) / a0;
        a1 = (poleW - k) / a0;
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s[0] = s[1] = 0.0;
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        {
            auto x1 = state[(size_t) ch][0], y1 = state[(size_t) ch][1];
            auto* x = channels[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto in = (double) x[i];
                const auto out = b0 * in + b1 * x1 - a1 * y1;
                x1 = in;
                y1 = out;
                x[i] = (float) out;
            }

            state[(size_t) ch][0] = x1;
            state[(size_t) ch][1] = y1;
        }
    }

private:
    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double state[2][2] {};
};

} // namespace encore::dsp
