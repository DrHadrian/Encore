// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

#include "engine/core/Block.h"

namespace encore::test
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;
inline const ProcessSpec spec { sampleRate, blockSize };

inline std::vector<float> sine (double frequency, float amplitude, int numSamples)
{
    std::vector<float> s ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        s[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate);

    return s;
}

inline std::vector<float> noise (float amplitude, int numSamples, int seed = 1)
{
    juce::Random random (seed);
    std::vector<float> s ((size_t) numSamples);

    for (auto& x : s)
        x = amplitude * (random.nextFloat() * 2.0f - 1.0f);

    return s;
}

//== Generated test material (native modules, decision 0062) =================
// Everything here is deterministic: the same seed gives the same samples on
// every machine, so measured tests are exact and repeatable. CI uses only
// these synthetic signals; real recordings stay on the player's computer.

/** An exponential (log) sweep from f0 to f1 Hz over numSamples: equal time per
    octave, the usual signal for measuring a response. */
inline std::vector<float> logSweep (double f0, double f1, float amplitude, int numSamples, double rate = sampleRate)
{
    std::vector<float> s ((size_t) numSamples);
    const auto seconds = numSamples / rate;
    const auto k = std::log (f1 / f0);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / rate;
        const auto phase = 2.0 * juce::MathConstants<double>::pi * f0 * seconds / k * (std::exp (t / seconds * k) - 1.0);
        s[(size_t) i] = amplitude * (float) std::sin (phase);
    }

    return s;
}

/** A single sample of `amplitude` at `position`, zeros elsewhere. */
inline std::vector<float> impulse (int numSamples, int position = 0, float amplitude = 1.0f)
{
    std::vector<float> s ((size_t) numSamples, 0.0f);

    if (position >= 0 && position < numSamples)
        s[(size_t) position] = amplitude;

    return s;
}

/** White noise, uniform in [-amplitude, amplitude] (the same as noise()). */
inline std::vector<float> whiteNoise (float amplitude, int numSamples, int seed = 1)
{
    return noise (amplitude, numSamples, seed);
}

/** Pink noise (-3 dB per octave), peak about `amplitude`. Voss-McCartney:
    the sum of 16 random rows, row r redrawn every 2^r samples, plus a white
    row; written here from the algorithm's description. */
inline std::vector<float> pinkNoise (float amplitude, int numSamples, int seed = 1)
{
    constexpr int rows = 16;
    juce::Random random (seed);
    float values[rows] {};
    float sum = 0.0f;

    for (auto& v : values)
    {
        v = random.nextFloat() * 2.0f - 1.0f;
        sum += v;
    }

    std::vector<float> s ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        // The row to redraw: the number of trailing zeros of the counter.
        const auto counter = (unsigned) i + 1u;
        int row = 0;

        while (row < rows - 1 && ((counter >> row) & 1u) == 0u)
            ++row;

        sum -= values[row];
        values[row] = random.nextFloat() * 2.0f - 1.0f;
        sum += values[row];

        const auto white = random.nextFloat() * 2.0f - 1.0f;
        s[(size_t) i] = amplitude * (sum + white) / (float) (rows + 1);
    }

    return s;
}

/** A slowly decaying sine: amplitude falls by decayDbPerSecond. */
inline std::vector<float> decayingSine (double frequency, float amplitude, double decayDbPerSecond, int numSamples,
                                        double rate = sampleRate)
{
    std::vector<float> s ((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / rate;
        const auto gain = std::pow (10.0, -decayDbPerSecond * t / 20.0);
        s[(size_t) i] = amplitude * (float) (gain * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * t));
    }

    return s;
}

/** A generated note with a known envelope over a known noise floor. */
struct GeneratedNote
{
    std::vector<float> signal;     // the note plus the noise floor
    std::vector<float> envelope;   // the note's peak envelope alone (linear), sample by sample
    int onset = 0;                 // first sample of the note
    float noiseFloor = 0.0f;       // the noise floor's peak (linear)
};

/** A plucked guitar-like note: silence (the noise floor), then at `onset` a
    2 ms rise and an exponential decay (`decayMs` = time to fall by 60 dB),
    made of a fundamental and a few falling harmonics. The noise floor is white
    noise of peak `noiseFloorDb` throughout. A palm-mute is the same with a
    short decay and fewer harmonics (palmMute()). */
inline GeneratedNote pluck (int numSamples, int onset = 4800, float amplitude = 0.5f, double decayMs = 2000.0,
                            float noiseFloorDb = -70.0f, double fundamental = 110.0, int harmonics = 6,
                            int seed = 1, double rate = sampleRate)
{
    GeneratedNote note;
    note.onset = onset;
    note.noiseFloor = juce::Decibels::decibelsToGain (noiseFloorDb);
    note.signal = noise (note.noiseFloor, numSamples, seed);
    note.envelope.assign ((size_t) numSamples, 0.0f);

    const auto riseSamples = juce::jmax (1, (int) (0.002 * rate));
    const auto decayPerSample = std::pow (10.0, -60.0 / 20.0 / (decayMs * 0.001 * rate));

    // Harmonic n at 1/n of the fundamental's level; normalised so the sum peaks at 1.
    auto norm = 0.0;

    for (int h = 1; h <= harmonics; ++h)
        norm += 1.0 / h;

    auto env = 0.0;

    for (int i = onset; i < numSamples; ++i)
    {
        const auto n = i - onset;
        env = n < riseSamples ? (double) (n + 1) / riseSamples : env * decayPerSample;
        note.envelope[(size_t) i] = amplitude * (float) env;

        auto tone = 0.0;
        const auto t = n / rate;

        for (int h = 1; h <= harmonics; ++h)
            tone += std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * h * t) / h;

        note.signal[(size_t) i] += (float) (amplitude * env * tone / norm);
    }

    return note;
}

/** A palm-muted note: a short, dark pluck (decay ~120 ms, 3 harmonics). */
inline GeneratedNote palmMute (int numSamples, int onset = 4800, float amplitude = 0.5f,
                               float noiseFloorDb = -70.0f, int seed = 1, double rate = sampleRate)
{
    return pluck (numSamples, onset, amplitude, 120.0, noiseFloorDb, 82.4, 3, seed, rate);
}

inline float rms (const std::vector<float>& s, int start, int end)
{
    double sum = 0.0;

    for (int i = start; i < end; ++i)
        sum += (double) s[(size_t) i] * s[(size_t) i];

    return (float) std::sqrt (sum / juce::jmax (1, end - start));
}

inline float peak (const std::vector<float>& s, int start, int end)
{
    float p = 0.0f;

    for (int i = start; i < end; ++i)
        p = juce::jmax (p, std::abs (s[(size_t) i]));

    return p;
}

inline float toDb (float gain) { return juce::Decibels::gainToDecibels (gain, -200.0f); }

/** Runs a mono signal through anything with process(float* const*, int, int), block by block. */
template <typename Processor>
std::vector<float> runMono (Processor& p, std::vector<float> signal)
{
    for (int offset = 0; offset < (int) signal.size(); offset += blockSize)
    {
        float* channels[1] = { signal.data() + offset };
        p.process (channels, 1, juce::jmin (blockSize, (int) signal.size() - offset));
    }

    return signal;
}

} // namespace encore::test
