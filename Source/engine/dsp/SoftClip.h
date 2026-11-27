// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <cmath>

namespace encore::dsp
{

/** A smooth, slightly asymmetric soft clipper, like a 9 V pedal's op-amp
    stage running into its rails (the Encore Engine's DSP, decision 0063).
    Shared: the Pedal EQ's HOT stage now, the drives later.

    The curve, for x above the linear limit L (and mirrored, with its own
    shoulder width, below -L):
        y = L + u tanh ((x - L) / u)
    - exactly linear (y = x, bit for bit) for |x| <= L;
    - at L the slope is 1 and the curvature 0 (tanh's), so the shoulder
      joins the straight line without a kink: low-order harmonics only, and
      they grow gently as the level rises;
    - u sets how soon it bends: it is solved so the gain is down by exactly
      1 dB at a chosen level, separately for each polarity;
    - it never exceeds L + u (the rail).

    The defaults are the Pedal EQ's (EQ-PLAN §3.1, Proposed, tuned on the rig
    in U1): linear to -6 dBFS; 1 dB down at +6 dBFS on the positive side and
    at +5 dBFS on the negative (the asymmetry: the rails aren't equal), so
    hard playing adds a little even-order warmth. Asymmetry makes a little DC
    when it clips: a module that uses it puts a DC blocker after it.

    Construction solves u (plain maths, bounded): any thread, before use.
    process(): audio thread, no state, no allocation.
*/
class SoftClip
{
public:
    static constexpr double defaultLinearLimitDb = -6.0;
    static constexpr double defaultPositiveOneDbPointDb = 6.0;
    static constexpr double defaultNegativeOneDbPointDb = 5.0;

    SoftClip (double linearLimitDb = defaultLinearLimitDb,
              double positiveOneDbPointDb = defaultPositiveOneDbPointDb,
              double negativeOneDbPointDb = defaultNegativeOneDbPointDb) noexcept
        : limit (gainFromDb (linearLimitDb)),
          positiveOneDb (gainFromDb (positiveOneDbPointDb)),
          negativeOneDb (gainFromDb (negativeOneDbPointDb)),
          positiveWidth (solveWidth (limit, positiveOneDb)),
          negativeWidth (solveWidth (limit, negativeOneDb))
    {
    }

    float process (float x) const noexcept
    {
        if (x > limit)
            return (float) (limit + positiveWidth * std::tanh ((x - limit) / positiveWidth));

        if (x < -limit)
            return (float) -(limit + negativeWidth * std::tanh ((-x - limit) / negativeWidth));

        return x;
    }

    /** The gain change (dB, <= 0) the curve makes at a level, either polarity. */
    double gainDbAt (double x) const noexcept
    {
        const auto y = (double) process ((float) x);
        return 20.0 * std::log10 (std::abs (y) / std::abs (x));
    }

    /** Above these magnitudes the curve takes off more than 1 dB (for HOT). */
    double getPositiveOneDbPoint() const noexcept { return positiveOneDb; }
    double getNegativeOneDbPoint() const noexcept { return negativeOneDb; }
    double getLinearLimit() const noexcept        { return limit; }

    /** The highest output each side can reach. */
    double getPositiveRail() const noexcept { return limit + positiveWidth; }
    double getNegativeRail() const noexcept { return limit + negativeWidth; }

private:
    static double gainFromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

    /** The shoulder width u at which the curve is 1 dB down at `point`.
        f(point) rises with u, so bisection finds it. */
    static double solveWidth (double linearLimit, double point) noexcept
    {
        const auto target = point * std::pow (10.0, -1.0 / 20.0);
        auto lo = 1.0e-6, hi = 1000.0;

        for (int i = 0; i < 200; ++i)
        {
            const auto u = 0.5 * (lo + hi);
            const auto y = linearLimit + u * std::tanh ((point - linearLimit) / u);
            (y < target ? lo : hi) = u;
        }

        return 0.5 * (lo + hi);
    }

    double limit, positiveOneDb, negativeOneDb, positiveWidth, negativeWidth;
};

} // namespace encore::dsp
