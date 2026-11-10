// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"

namespace encore
{

/** A noise gate tuned for guitar: opens instantly on a pick attack, holds
    briefly, then closes with a smooth release so sustained notes don't chop.

    Opens when the level rises above the threshold and closes only after it
    falls 6 dB below it (hysteresis), so a note decaying around the threshold
    doesn't chatter. All channels share one detector.

    "Detect: guitar" makes it listen to the clean guitar input while cutting
    the signal where it sits. Placed after a high-gain amp, that silences the
    amp's hiss with the clean signal's clear note endings.
*/
class NoiseGateBlock final : public Block
{
public:
    static constexpr auto typeId = "gate";

    NoiseGateBlock();

    /** For the UI: is the gate currently open? */
    bool isOpen() const noexcept { return open.load (std::memory_order_relaxed); }

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    Parameter& threshold;
    Parameter& release;
    Parameter& range;
    Parameter& detectGuitar;

    double sampleRate = 48000.0;
    float envelope = 0.0f;
    float envelopeDecay = 0.0f;
    float gain = 1.0f;
    float attackCoefficient = 0.0f;
    int holdSamplesTotal = 0;
    int holdCounter = 0;
    bool isGateOpen = false;

    std::atomic<bool> open { false };
};

} // namespace encore
