// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

#include "engine/PeakMeter.h"
#include "engine/dsp/OutputEq.h"
#include "engine/dsp/SafetyLimiter.h"
#include "model/Routing.h"

namespace encore
{

/** Routing as the audio thread uses it: compiled from this computer's
    RoutingConfig by the engine on the message thread. Plain values only.

    Inputs and outputs are referred to by slot (0..7), not by their place in
    the list: an output keeps its slot (and with it its EQ, delay line and
    meter) while others are added, removed or reordered, and patches send to
    slots, so reordering never sends a patch to the wrong place. */
struct RoutingTable
{
    struct Input
    {
        int slot = 0;
        int channel = 1;                 // the interface's input, 1-based
    };

    struct Output
    {
        int slot = 0;
        std::array<int, 2> channels {};  // the interface's outputs, 1-based
        int numChannels = 0;             // 1, 2, or 0 = all outputs
        OutputEq eq;
        float delayMs = 0.0f;
        bool fresh = false;              // a new output in this slot: start from a clean state
    };

    std::array<Input, (size_t) routing::maxInputs> inputs {};
    int numInputs = 0;
    std::array<Output, (size_t) routing::maxOutputs> outputs {};
    int numOutputs = 0;
};

/** One output's processing, after the patches are mixed into it (always as a
    stereo pair: a mono output takes (L + R) / 2 at the very end):
    level and mute (ramped), EQ, delay, NaN guard, its own safety limiter, a
    hard ceiling and the meter. Fixed memory, sized by prepare(). */
class OutputStrip
{
public:
    static constexpr double levelRampSeconds = 0.01;
    static constexpr double delayCrossfadeSeconds = 0.01;
    static constexpr float hardCeiling = 0.999f;

    OutputStrip() = default;

    //== While audio is stopped ================================================
    void prepare (double sampleRate, int maxBlockSize);

    //== Audio thread ==========================================================
    /** Back to a clean state: no delay or filter history, the level fading in from silence. */
    void reset() noexcept;

    /** The table's settings (EQ glides, delay crossfades). */
    void setSettings (const OutputEq& eq, float delayMs) noexcept;

    /** Processes the pair in place. gainTarget = level x mute, ramped here.
        Returns true if the hard ceiling had to act. */
    bool process (float* left, float* right, int numSamples, float gainTarget) noexcept;

    //== Any thread (meters) ===================================================
    PeakMeter& getMeter (int channel) noexcept   { return meters[(size_t) juce::jlimit (0, 1, channel)]; }

    /** Deepest limiter gain reduction since the last call (dB, 0 = idle). */
    float takeLimiterReductionDb() noexcept      { return limiter.takeGainReductionDb(); }

    /** Buffers of broken audio (NaN/inf) silenced here (should stay 0). */
    int getBadBufferCount() const noexcept       { return badBuffers.load (std::memory_order_relaxed); }

private:
    float readDelayed (int channel, int delay) const noexcept;

    double sampleRate = 48000.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 0.0f };
    dsp::OutputEqFilter eq;
    dsp::SafetyLimiter limiter;

    // Delay: a line per channel, always written so a change can crossfade.
    std::array<std::vector<float>, 2> lines;
    int lineSize = 0, writeIndex = 0;
    int delayNow = 0, delayBefore = 0, fadeLeft = 0, fadeLength = 1;

    std::array<PeakMeter, 2> meters;
    std::atomic<int> badBuffers { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputStrip)
};

} // namespace encore
