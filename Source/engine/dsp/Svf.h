// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <array>
#include <cmath>

#include "engine/dsp/FilterDesign.h"

namespace encore::dsp
{

/** Coefficients of the TPT state-variable filter (Svf below).

    The filter is the zero-delay-feedback ("topology-preserving transform")
    state-variable filter described by Zavalishin ("The Art of VA Filter
    Design") and by Simper's SVF notes, written here in our own words: two
    trapezoidal integrators with gain g = tan(pi fc / fs) and damping k = 1/Q,
    and the output a mix m0 * input + m1 * band pass + m2 * low pass.

    Any stable second-order section can be played by it: fromDesign() finds
    the g, k and mix that give exactly that transfer function (the inverse of
    the bilinear transform), so the decramped designs of FilterDesign.h run on
    the same structure. Why this structure: its states are the integrators'
    memories, which stay meaningful when the coefficients change, so it stays
    stable and click-free under fast modulation (a pedal sweeping a band),
    where direct-form biquads can blow up or zip.
*/
struct SvfCoefficients
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;   // derived from g and k
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;   // output mix (defaults: pass through)

    static SvfCoefficients fromGainAndDamping (double g, double k, double m0, double m1, double m2) noexcept
    {
        SvfCoefficients c;
        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        c.m0 = m0;
        c.m1 = m1;
        c.m2 = m2;
        return c;
    }

    /** The SVF that plays this section exactly. The section must be stable. */
    static SvfCoefficients fromDesign (const BiquadDesign& d) noexcept
    {
        // The bilinear image of (s^2 + k s + 1) with gain g has, normalised,
        //   1 + a1 + a2 = 4 g^2 / D,  1 - a1 + a2 = 4 / D,  1 - a2 = 2 k g / D,
        // with D = 1 + k g + g^2. Solve for g, k and D. The analog numerator
        // n0 s^2 + n1 s + n2 then follows from b0 + b1 + b2 = 4 n2 g^2 / D,
        // b0 - b1 + b2 = 4 n0 / D and b0 - b2 = 2 n1 g / D. The filter's
        // output is m0 (s^2 + k s + 1) + m1 s + m2 over the same denominator
        // (the input itself, the band pass, the low pass), so
        // m0 = n0, m1 = n1 - k n0, m2 = n2 - n0.
        const auto atDc = 1.0 + d.a1 + d.a2;
        const auto atNyquist = 1.0 - d.a1 + d.a2;

        if (! (atDc > 0.0 && atNyquist > 0.0))
            return {};   // not a stable section: pass through rather than blow up

        const auto g = std::sqrt (atDc / atNyquist);
        const auto D = 4.0 / atNyquist;
        const auto k = (1.0 - d.a2) * D / (2.0 * g);

        const auto n0 = (d.b0 - d.b1 + d.b2) * D * 0.25;
        const auto n1 = (d.b0 - d.b2) * D / (2.0 * g);
        const auto n2 = (d.b0 + d.b1 + d.b2) * D / (4.0 * g * g);

        return fromGainAndDamping (g, k, n0, n1 - k * n0, n2 - n0);
    }
};

/** One TPT state-variable filter section for up to MaxChannels channels.
    States in double. Audio thread: no allocation, no locks. */
template <int MaxChannels>
class Svf
{
public:
    void reset() noexcept
    {
        ic1.fill (0.0);
        ic2.fill (0.0);
    }

    double tick (double x, int channel, const SvfCoefficients& c) noexcept
    {
        auto& s1 = ic1[(size_t) channel];
        auto& s2 = ic2[(size_t) channel];
        const auto v3 = x - s2;
        const auto v1 = c.a1 * s1 + c.a2 * v3;          // band pass
        const auto v2 = s2 + c.a2 * s1 + c.a3 * v3;     // low pass
        s1 = 2.0 * v1 - s1;
        s2 = 2.0 * v2 - s2;
        return c.m0 * x + c.m1 * v1 + c.m2 * v2;
    }

private:
    std::array<double, (size_t) MaxChannels> ic1 {}, ic2 {};
};

} // namespace encore::dsp
