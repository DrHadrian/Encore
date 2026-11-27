// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/dsp/FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double gainFromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }
    double dbFromPower (double power) noexcept { return 10.0 * std::log10 (std::max (power, 1.0e-30)); }

    /** The impulse-invariant image of an analog pole pair with natural angular
        frequency w (radians per sample) and damping zeta: 1 + a1 z^-1 + a2 z^-2.
        The pole frequency is kept below Nyquist (a pole above it can't be
        represented; the matched numerator then absorbs the difference). */
    void impulseInvariantPoles (double w, double zeta, double& a1, double& a2) noexcept
    {
        w = std::min (w, 0.9 * pi);
        const auto decay = std::exp (-zeta * w);

        if (zeta <= 1.0)
            a1 = -2.0 * decay * std::cos (std::sqrt (1.0 - zeta * zeta) * w);
        else
            a1 = -2.0 * decay * std::cosh (std::sqrt (zeta * zeta - 1.0) * w);

        a2 = decay * decay;
    }

    /** Solves the numerator so that |H|^2 equals the given values at DC, at
        Nyquist and at wMatch, for the poles a1, a2.

        With phi0 = cos^2(w/2), phi1 = sin^2(w/2), phi2 = 4 phi0 phi1, the
        squared magnitude of any second-order polynomial p0 + p1 z^-1 + p2 z^-2
        is P0 phi0 + P1 phi1 + P2 phi2, where P0 = (p0+p1+p2)^2,
        P1 = (p0-p1+p2)^2 and P2 = -4 p0 p2. Matching three frequencies is
        three linear equations in B0, B1, B2; the coefficients follow, taking
        the minimum-phase root. */
    BiquadDesign matchNumerator (double a1, double a2, double powerAtDc, double powerAtNyquist,
                                 double wMatch, double powerAtMatch) noexcept
    {
        const auto A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
        const auto A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
        const auto A2 = -4.0 * a2;

        const auto s = std::sin (wMatch * 0.5);
        const auto phi1 = s * s;
        const auto phi0 = 1.0 - phi1;
        const auto phi2 = 4.0 * phi0 * phi1;

        const auto B0 = powerAtDc * A0;
        const auto B1 = powerAtNyquist * A1;
        const auto B2 = (powerAtMatch * (A0 * phi0 + A1 * phi1 + A2 * phi2) - B0 * phi0 - B1 * phi1) / phi2;

        const auto sum = std::sqrt (B0);         // b0 + b1 + b2
        const auto alternating = std::sqrt (B1); // b0 - b1 + b2
        const auto w = 0.5 * (sum + alternating);

        BiquadDesign d;
        d.b0 = 0.5 * (w + std::sqrt (std::max (0.0, w * w + B2)));
        d.b1 = 0.5 * (sum - alternating);
        d.b2 = w - d.b0;
        d.a1 = a1;
        d.a2 = a2;
        return d;
    }

    /** 1 / H: swaps numerator and denominator (H must be minimum phase). */
    BiquadDesign reciprocal (const BiquadDesign& h) noexcept
    {
        BiquadDesign r;
        r.b0 = 1.0 / h.b0;
        r.b1 = h.a1 / h.b0;
        r.b2 = h.a2 / h.b0;
        r.a1 = h.b1 / h.b0;
        r.a2 = h.b2 / h.b0;
        return r;
    }

    double bellPower (double ratio, double q, double gain) noexcept
    {
        // (s^2 + s A/Q + 1) / (s^2 + s/(A Q) + 1), A = sqrt(G), s = j ratio
        const auto A = std::sqrt (gain);
        const auto re = 1.0 - ratio * ratio;
        const auto num = re * re + (ratio * A / q) * (ratio * A / q);
        const auto den = re * re + (ratio / (A * q)) * (ratio / (A * q));
        return num / den;
    }

    double lowShelfPower (double ratio, double q, double gain) noexcept
    {
        // A (s^2 + sqrt(A)/Q s + A) / (A s^2 + sqrt(A)/Q s + 1)
        const auto A = std::sqrt (gain);
        const auto r2 = ratio * ratio;
        const auto c = std::sqrt (A) / q * ratio;
        const auto num = (A - r2) * (A - r2) + c * c;
        const auto den = (1.0 - A * r2) * (1.0 - A * r2) + c * c;
        return A * A * num / den;
    }

    /** A low-shelf boost (gain >= 1), matched: the base of every shelf. */
    BiquadDesign matchedLowShelfBoost (double sampleRate, double frequency, double q, double gain) noexcept
    {
        const auto w0 = 2.0 * pi * frequency / sampleRate;
        const auto A = std::sqrt (gain);
        double a1, a2;
        impulseInvariantPoles (w0 / std::sqrt (A), 0.5 / q, a1, a2);   // poles of A s^2 + sqrt(A)/Q s + 1

        return matchNumerator (a1, a2, lowShelfPower (1.0e-9, q, gain),
                               lowShelfPower (0.5 * sampleRate / frequency, q, gain),
                               w0, lowShelfPower (1.0, q, gain));
    }

    /** Bilinear transform of (n0 s^2 + n1 s + n2) / (s^2 + s/Q + 1), prewarped at the corner. */
    BiquadDesign bilinear (double sampleRate, double frequency, double q, double n0, double n1, double n2) noexcept
    {
        const auto g = std::tan (pi * design::clampFrequency (frequency, sampleRate) / sampleRate);
        const auto k = 1.0 / q;
        const auto d = 1.0 + k * g + g * g;

        BiquadDesign r;
        r.b0 = (n0 + n1 * g + n2 * g * g) / d;
        r.b1 = 2.0 * (n2 * g * g - n0) / d;
        r.b2 = (n0 - n1 * g + n2 * g * g) / d;
        r.a1 = 2.0 * (g * g - 1.0) / d;
        r.a2 = (1.0 - k * g + g * g) / d;
        return r;
    }

    double clampQ (double q) noexcept { return std::clamp (q, 0.01, 100.0); }
}

//==============================================================================
std::complex<double> BiquadDesign::responseAt (double w) const noexcept
{
    const auto z1 = std::polar (1.0, -w);
    const auto z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

double BiquadDesign::magnitudeDb (double frequency, double sampleRate) const noexcept
{
    return dbFromPower (std::norm (responseAt (2.0 * pi * frequency / sampleRate)));
}

bool BiquadDesign::isStable() const noexcept
{
    return std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
}

//==============================================================================
double design::clampFrequency (double frequency, double sampleRate) noexcept
{
    return std::clamp (frequency, 1.0, maxFrequencyRatio * sampleRate);
}

BiquadDesign design::matchedBell (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    frequency = clampFrequency (frequency, sampleRate);
    q = clampQ (q);
    const auto gain = gainFromDb (std::abs (gainDb));
    const auto w0 = 2.0 * pi * frequency / sampleRate;

    // The boost's poles: s^2 + s/(A Q) + 1.
    double a1, a2;
    impulseInvariantPoles (w0, 0.5 / (q * std::sqrt (gain)), a1, a2);

    const auto boost = matchNumerator (a1, a2, 1.0, bellPower (0.5 * sampleRate / frequency, q, gain),
                                       w0, gain * gain);
    return gainDb >= 0.0 ? boost : reciprocal (boost);
}

BiquadDesign design::matchedLowShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    frequency = clampFrequency (frequency, sampleRate);
    const auto boost = matchedLowShelfBoost (sampleRate, frequency, clampQ (q), gainFromDb (std::abs (gainDb)));
    return gainDb >= 0.0 ? boost : reciprocal (boost);
}

BiquadDesign design::matchedHighShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    // High shelf (G) = G / low shelf (G), the same identity as the analog prototypes.
    frequency = clampFrequency (frequency, sampleRate);
    const auto gain = gainFromDb (std::abs (gainDb));
    const auto lowBoost = matchedLowShelfBoost (sampleRate, frequency, clampQ (q), gain);

    if (gainDb >= 0.0)
    {
        auto r = reciprocal (lowBoost);
        r.b0 *= gain;
        r.b1 *= gain;
        r.b2 *= gain;
        return r;
    }

    auto r = lowBoost;
    r.b0 /= gain;
    r.b1 /= gain;
    r.b2 /= gain;
    return r;
}

BiquadDesign design::lowPass (double sampleRate, double frequency, double q) noexcept
{
    return bilinear (sampleRate, frequency, clampQ (q), 0.0, 0.0, 1.0);
}

BiquadDesign design::highPass (double sampleRate, double frequency, double q) noexcept
{
    return bilinear (sampleRate, frequency, clampQ (q), 1.0, 0.0, 0.0);
}

BiquadDesign design::bandPass (double sampleRate, double frequency, double q) noexcept
{
    q = clampQ (q);
    return bilinear (sampleRate, frequency, q, 0.0, 1.0 / q, 0.0);
}

BiquadDesign design::notch (double sampleRate, double frequency, double q) noexcept
{
    return bilinear (sampleRate, frequency, clampQ (q), 1.0, 0.0, 1.0);
}

BiquadDesign design::allPass (double sampleRate, double frequency, double q) noexcept
{
    q = clampQ (q);
    return bilinear (sampleRate, frequency, q, 1.0, -1.0 / q, 1.0);
}

BiquadDesign design::firstOrderLowPass (double sampleRate, double frequency) noexcept
{
    const auto g = std::tan (pi * clampFrequency (frequency, sampleRate) / sampleRate);
    BiquadDesign r;
    r.b0 = r.b1 = g / (1.0 + g);
    r.a1 = (g - 1.0) / (1.0 + g);
    return r;
}

BiquadDesign design::firstOrderHighPass (double sampleRate, double frequency) noexcept
{
    const auto g = std::tan (pi * clampFrequency (frequency, sampleRate) / sampleRate);
    BiquadDesign r;
    r.b0 = 1.0 / (1.0 + g);
    r.b1 = -r.b0;
    r.a1 = (g - 1.0) / (1.0 + g);
    return r;
}

double design::butterworthQ (int order, int index) noexcept
{
    // The poles of an order-n Butterworth lie evenly on the unit circle; pair m
    // makes the angle pi (n - 1 - 2m) / (2n) with the negative real axis
    // (n = 2: 45 degrees, Q 0.707; n = 3: 60 degrees, Q 1; n = 4: Q 1.31 and 0.54).
    return 1.0 / (2.0 * std::cos ((order - 1.0 - 2.0 * index) * pi / (2.0 * order)));
}

//==============================================================================
double analog::bellDb (double frequency, double centre, double q, double gainDb) noexcept
{
    const auto gain = gainFromDb (std::abs (gainDb));
    const auto db = dbFromPower (bellPower (frequency / centre, q, gain));
    return gainDb >= 0.0 ? db : -db;
}

double analog::lowShelfDb (double frequency, double centre, double q, double gainDb) noexcept
{
    const auto gain = gainFromDb (std::abs (gainDb));
    const auto db = dbFromPower (lowShelfPower (frequency / centre, q, gain));
    return gainDb >= 0.0 ? db : -db;
}

double analog::highShelfDb (double frequency, double centre, double q, double gainDb) noexcept
{
    const auto gain = gainFromDb (std::abs (gainDb));
    const auto db = 20.0 * std::log10 (gain) - dbFromPower (lowShelfPower (frequency / centre, q, gain));
    return gainDb >= 0.0 ? db : -db;
}

double analog::butterworthLowPassDb (double frequency, double corner, int order) noexcept
{
    return -dbFromPower (1.0 + std::pow (frequency / corner, 2.0 * order));
}

double analog::butterworthHighPassDb (double frequency, double corner, int order) noexcept
{
    return -dbFromPower (1.0 + std::pow (corner / frequency, 2.0 * order));
}

} // namespace encore::dsp
