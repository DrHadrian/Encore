// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <array>
#include <cmath>

#include "engine/dsp/Biquad.h"
#include "model/Routing.h"

namespace encore::dsp
{

/** An output's EQ (milestone 11): low cut, bass shelf, mid peak, treble shelf,
    high cut, the same shapes as the EQ block (fixed mid width), so the UI's
    curve and the sound agree. Stereo, never allocates. */
namespace outputEq
{
    constexpr double bassShelfHz = 120.0;     // as the EQ block
    constexpr double trebleShelfHz = 3500.0;
    constexpr double butterworthQ = 0.7071;
    constexpr int numStages = 5;

    enum Stage { lowCutStage, bassStage, midStage, trebleStage, highCutStage };

    /** Which stages change the sound (a cut at its "off" end or a flat
        shelf/peak is skipped, as in the EQ block). */
    inline std::array<bool, numStages> activeStages (const OutputEq& eq) noexcept
    {
        return { eq.lowCut > OutputEq::minLowCut + 0.5f,
                 std::abs (eq.bass) > 0.05f,
                 std::abs (eq.mid) > 0.05f,
                 std::abs (eq.treble) > 0.05f,
                 eq.highCut < OutputEq::maxHighCut - 100.0f };
    }

    inline std::array<BiquadCoefficients, numStages> coefficients (const OutputEq& eq, double sampleRate) noexcept
    {
        using C = BiquadCoefficients;
        return { C::highPass  (sampleRate, eq.lowCut, butterworthQ),
                 C::lowShelf  (sampleRate, bassShelfHz, eq.bass),
                 C::peak      (sampleRate, eq.midFreq, OutputEq::midQ, eq.mid),
                 C::highShelf (sampleRate, trebleShelfHz, eq.treble),
                 C::lowPass   (sampleRate, eq.highCut, butterworthQ) };
    }

    /** The EQ's response at a frequency, in dB (for the UI's curve). */
    inline double responseDb (const OutputEq& eq, double frequency, double sampleRate = 48000.0) noexcept
    {
        const auto active = activeStages (eq);
        const auto c = coefficients (eq, sampleRate);
        double magnitude = 1.0;

        for (size_t s = 0; s < (size_t) numStages; ++s)
            if (active[s])
                magnitude *= c[s].magnitudeAt (sampleRate, frequency);

        return 20.0 * std::log10 (std::fmax (magnitude, 1.0e-9));
    }
}

/** The filter itself. A changed setting glides there (about 10 ms, the
    coefficients updated every 32 samples), so moving a knob never clicks. */
class OutputEqFilter
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : stages)
            s.reset();

        current = target;
        active = {};
        coefficientsValid = false;
    }

    /** Audio thread: where to go (from the routing table). */
    void setTarget (const OutputEq& eq) noexcept   { target = eq; }

    /** Audio thread: process (in place) numChannels (1 or 2). */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        for (int offset = 0; offset < numSamples;)
        {
            const auto moving = ! (current == target);
            const auto chunk = moving ? juce::jmin (updateInterval, numSamples - offset) : numSamples - offset;

            if (moving)
                glide (chunk);

            if (moving || ! coefficientsValid)
                update();

            for (size_t s = 0; s < (size_t) outputEq::numStages; ++s)
                if (active[s])
                    for (int ch = 0; ch < numChannels; ++ch)
                        stages[s].process (channels[ch] + offset, ch, chunk);

            offset += chunk;
        }
    }

    /** True when it changes nothing now (and isn't moving). */
    bool isFlat() const noexcept   { return current == target && current.isFlat(); }

private:
    static constexpr int updateInterval = 32;
    static constexpr double glideSeconds = 0.01;

    void glide (int numSamples) noexcept
    {
        const auto k = (float) (1.0 - std::exp (-numSamples / (glideSeconds * sampleRate)));

        auto towards = [k] (float& value, float goal, float snap)
        {
            value += (goal - value) * k;

            if (std::abs (goal - value) < snap)
                value = goal;
        };

        // Frequencies in octaves, so a sweep sounds even.
        auto towardsHz = [k] (float& value, float goal)
        {
            const auto logValue = std::log2 (value), logGoal = std::log2 (goal);
            const auto next = logValue + (logGoal - logValue) * k;
            value = std::abs (logGoal - next) < 0.002f ? goal : std::exp2 (next);
        };

        towardsHz (current.lowCut, target.lowCut);
        towardsHz (current.highCut, target.highCut);
        towardsHz (current.midFreq, target.midFreq);
        towards (current.bass, target.bass, 0.01f);
        towards (current.mid, target.mid, 0.01f);
        towards (current.treble, target.treble, 0.01f);
    }

    void update() noexcept
    {
        const auto wanted = outputEq::activeStages (current);
        const auto c = outputEq::coefficients (current, sampleRate);

        for (size_t s = 0; s < (size_t) outputEq::numStages; ++s)
        {
            // A stage coming back starts from a clean state (switching happens
            // where it is inaudibly close to flat, so this never clicks).
            if (wanted[s] && ! active[s])
                stages[s].reset();

            active[s] = wanted[s];
            stages[s].setCoefficients (c[s]);
        }

        coefficientsValid = true;
    }

    double sampleRate = 48000.0;
    OutputEq current, target;
    std::array<Biquad<2>, (size_t) outputEq::numStages> stages;
    std::array<bool, (size_t) outputEq::numStages> active {};
    bool coefficientsValid = false;
};

} // namespace encore::dsp
