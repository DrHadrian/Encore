// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"

namespace encore
{

/** A simple digital delay: time, feedback, mix, with a gentle low-pass in the
    feedback path so repeats darken like a classic delay pedal.

    First reason it exists: so patch switching can be heard letting repeats
    ring out. It's the starting point for the full built-in delay later
    (tap tempo, sync, ping-pong, modulation).
*/
class DelayBlock final : public Block
{
public:
    static constexpr auto typeId = "delay";
    static constexpr double maxDelaySeconds = 2.0;

    DelayBlock();

    bool hasTail() const noexcept override { return true; }

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    Parameter& time;
    Parameter& feedback;
    Parameter& mix;

    std::vector<float> buffer;
    int writeIndex = 0;
    double sampleRate = 48000.0;
    float toneState = 0.0f, toneCoefficient = 0.0f;
};

} // namespace encore
