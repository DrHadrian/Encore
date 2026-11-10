// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <array>
#include <cmath>

namespace encore::dsp
{

/** Second-order filter coefficients (RBJ "Audio EQ Cookbook").

    Unlike juce::dsp::IIR::Coefficients, computing these never allocates, so
    they can be recalculated on the audio thread while a knob is moving.
*/
struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // normalised: a0 == 1

    static BiquadCoefficients highPass (double sampleRate, double frequency, double q) noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0,
                          1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    static BiquadCoefficients lowPass (double sampleRate, double frequency, double q) noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0,
                          1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    static BiquadCoefficients peak (double sampleRate, double frequency, double q, double gainDb) noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto cosw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const auto a = std::pow (10.0, gainDb / 40.0);
        return normalise (1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a,
                          1.0 + alpha / a, -2.0 * cosw, 1.0 - alpha / a);
    }

    static BiquadCoefficients lowShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto cosw = std::cos (w);
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);   // shelf slope S = 1
        const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;
        return normalise (a * ((a + 1.0) - (a - 1.0) * cosw + twoSqrtAAlpha),
                          2.0 * a * ((a - 1.0) - (a + 1.0) * cosw),
                          a * ((a + 1.0) - (a - 1.0) * cosw - twoSqrtAAlpha),
                          (a + 1.0) + (a - 1.0) * cosw + twoSqrtAAlpha,
                          -2.0 * ((a - 1.0) + (a + 1.0) * cosw),
                          (a + 1.0) + (a - 1.0) * cosw - twoSqrtAAlpha);
    }

    static BiquadCoefficients highShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto cosw = std::cos (w);
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
        const auto twoSqrtAAlpha = 2.0 * std::sqrt (a) * alpha;
        return normalise (a * ((a + 1.0) + (a - 1.0) * cosw + twoSqrtAAlpha),
                          -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw),
                          a * ((a + 1.0) + (a - 1.0) * cosw - twoSqrtAAlpha),
                          (a + 1.0) - (a - 1.0) * cosw + twoSqrtAAlpha,
                          2.0 * ((a - 1.0) - (a + 1.0) * cosw),
                          (a + 1.0) - (a - 1.0) * cosw - twoSqrtAAlpha);
    }

    /** Magnitude response at a frequency (used by tests and, later, EQ curves in the UI). */
    double magnitudeAt (double sampleRate, double frequency) const noexcept
    {
        const auto w = omega (sampleRate, frequency);
        const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const auto numRe = b0 + b1 * c1 + b2 * c2, numIm = -(b1 * s1 + b2 * s2);
        const auto denRe = 1.0 + a1 * c1 + a2 * c2, denIm = -(a1 * s1 + a2 * s2);
        return std::sqrt ((numRe * numRe + numIm * numIm) / (denRe * denRe + denIm * denIm));
    }

private:
    static double omega (double sampleRate, double frequency) noexcept
    {
        // Keep the corner safely below Nyquist whatever the device rate.
        const auto f = std::fmin (std::fmax (frequency, 10.0), sampleRate * 0.45);
        return 2.0 * 3.14159265358979323846 * f / sampleRate;
    }

    static BiquadCoefficients normalise (double nb0, double nb1, double nb2,
                                         double na0, double na1, double na2) noexcept
    {
        return { nb0 / na0, nb1 / na0, nb2 / na0, na1 / na0, na2 / na0 };
    }
};

/** One biquad with independent state per channel (transposed direct form II,
    double precision so low corners stay stable at small buffer sizes).
*/
template <int MaxChannels>
class Biquad
{
public:
    void setCoefficients (const BiquadCoefficients& c) noexcept { coeffs = c; }

    void reset() noexcept { state = {}; }

    float processSample (int channel, float input) noexcept
    {
        auto& s = state[(size_t) channel];
        const double x = input;
        const double y = coeffs.b0 * x + s[0];
        s[0] = coeffs.b1 * x - coeffs.a1 * y + s[1];
        s[1] = coeffs.b2 * x - coeffs.a2 * y;
        return (float) y;
    }

    void process (float* samples, int channel, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (channel, samples[i]);
    }

private:
    BiquadCoefficients coeffs;
    std::array<std::array<double, 2>, (size_t) MaxChannels> state {};
};

} // namespace encore::dsp
