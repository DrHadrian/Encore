// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <complex>

namespace encore::dsp
{

/** A second-order section's transfer function, normalised (a0 = 1), in double:
        H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)

    Part of the Encore Engine's DSP (native modules, decision 0062). Every
    filter the modules use is designed as one or more of these and played by
    the TPT state-variable filter (Svf.h), and the response the UI draws is
    computed from the very same numbers (FilterBand.h), so a curve on screen
    is exactly what you hear.

    All functions here are plain maths: no allocation, no state, safe on any
    thread. Frequencies are in Hz and are clamped just below Nyquist.
*/
struct BiquadDesign
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** The complex response at a normalised angular frequency w (0..pi). */
    std::complex<double> responseAt (double w) const noexcept;

    /** |H| in dB at a frequency. */
    double magnitudeDb (double frequency, double sampleRate) const noexcept;

    /** Both poles strictly inside the unit circle. */
    bool isStable() const noexcept;
};

namespace design
{
    /** The highest frequency a design accepts, as a share of the sample rate. */
    constexpr double maxFrequencyRatio = 0.49;

    double clampFrequency (double frequency, double sampleRate) noexcept;

    //== Decramped ("matched") sections ========================================
    // The magnitude is matched to the analog prototype up to Nyquist, instead
    // of being squeezed towards it as the bilinear transform does ("cramping").
    // Method (our own working of Vicanek, "Matched second order digital
    // filters", 2016): the poles come from the impulse-invariant transform of
    // the analog poles; the numerator is solved so the magnitude equals the
    // prototype's exactly at DC, at the centre frequency and at Nyquist.
    // A cut is the exact reciprocal of the matching boost (true of the analog
    // prototypes), and a high shelf is G divided by the low shelf, so every
    // case is solved from a well-behaved boost: always stable, always minimum
    // phase.

    /** Peaking bell, gain in dB, Q of the boost (cuts mirror it). */
    BiquadDesign matchedBell (double sampleRate, double frequency, double q, double gainDb) noexcept;

    /** Second-order shelves; frequency = the half-gain point, q 0.707 = no overshoot. */
    BiquadDesign matchedLowShelf (double sampleRate, double frequency, double q, double gainDb) noexcept;
    BiquadDesign matchedHighShelf (double sampleRate, double frequency, double q, double gainDb) noexcept;

    /** First-order (6 dB/oct) shelves, frequency = the half-gain point: the
        pole impulse-invariant, the numerator matched at DC and at Nyquist. */
    BiquadDesign matchedFirstOrderLowShelf (double sampleRate, double frequency, double gainDb) noexcept;
    BiquadDesign matchedFirstOrderHighShelf (double sampleRate, double frequency, double gainDb) noexcept;

    //== Bilinear (TPT) sections ===============================================
    // Prewarped at the corner, exactly what the TPT state-variable filter
    // computes natively. Used for cuts, notches, band passes and all passes.

    BiquadDesign lowPass (double sampleRate, double frequency, double q) noexcept;
    BiquadDesign highPass (double sampleRate, double frequency, double q) noexcept;
    BiquadDesign bandPass (double sampleRate, double frequency, double q) noexcept;   // 0 dB at the centre
    BiquadDesign notch (double sampleRate, double frequency, double q) noexcept;
    BiquadDesign allPass (double sampleRate, double frequency, double q) noexcept;
    BiquadDesign firstOrderLowPass (double sampleRate, double frequency) noexcept;
    BiquadDesign firstOrderHighPass (double sampleRate, double frequency) noexcept;
    BiquadDesign firstOrderAllPass (double sampleRate, double frequency) noexcept;   // -90 degrees at the frequency

    /** Q of section `index` (0-based) of a Butterworth filter of `order`
        (pairs of poles; an odd order adds one first-order section). */
    double butterworthQ (int order, int index) noexcept;
}

namespace analog
{
    // The analog prototypes' magnitudes (dB), the reference the decramped
    // sections are tested against and what the plans call "the analog curve".
    double bellDb (double frequency, double centre, double q, double gainDb) noexcept;
    double lowShelfDb (double frequency, double centre, double q, double gainDb) noexcept;
    double highShelfDb (double frequency, double centre, double q, double gainDb) noexcept;

    /** Butterworth shelves of `order` (1 = 6 dB/oct): |H|^2 = (G^2 + x^2N) / (1 + x^2N),
        with the half-gain point (in dB) at `centre`. */
    double butterworthLowShelfDb (double frequency, double centre, int order, double gainDb) noexcept;
    double butterworthHighShelfDb (double frequency, double centre, int order, double gainDb) noexcept;

    /** Butterworth of `order` (1 = 6 dB/oct). */
    double butterworthLowPassDb (double frequency, double corner, int order) noexcept;
    double butterworthHighPassDb (double frequency, double corner, int order) noexcept;
}

} // namespace encore::dsp
