// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "model/ShowModel.h"

namespace encore
{

/** The start of split paths (milestone 11): the signal goes to path A (the
    blocks after it in the chain) and path B (a second row) at once. It does
    no processing itself: its on/off is path B on/off, so a stomp switch can
    turn the second amp on and off. The chain does the splitting. */
class SplitBlock final : public Block
{
public:
    static constexpr const char* typeId = splitBlockType;

    SplitBlock() : Block (typeId, "Split") {}

private:
    void prepareBlock (const ProcessSpec&) override {}
    void processBlock (float* const*, int, int, const ProcessContext&) noexcept override {}
    void resetBlock() noexcept override {}
};

/** Where split paths come back together: a level and a pan for each path,
    always stereo out. Pan is a balance: centre keeps a path at full level on
    both sides, hard left puts it on the left only. A level at its bottom
    (-60 dB) is off. The chain calls mix(); its own on/off isn't used. */
class MergeBlock final : public Block
{
public:
    static constexpr const char* typeId = mergeBlockType;
    static constexpr float minLevelDb = -60.0f;   // and below: off
    static constexpr float maxLevelDb = 6.0f;

    MergeBlock();

    int getOutputChannels (int) const noexcept override { return 2; }

    /** Audio thread: path A and path B (each 1 or 2 channels) into out (2
        channels; may be path A's buffers). bGain fades path B (0..1, per
        sample from the chain's path B ramp, or null for 1). */
    void mix (const float* const* a, int numA, const float* const* b, int numB,
              float* const* out, int numSamples, const float* bGain) noexcept;

    /** dB to gain, the bottom of the range being off. */
    static float levelToGain (float dB) noexcept;

private:
    void prepareBlock (const ProcessSpec&) override {}
    void processBlock (float* const*, int, int, const ProcessContext&) noexcept override {}
    void resetBlock() noexcept override {}

    Parameter& levelA;
    Parameter& panA;
    Parameter& levelB;
    Parameter& panB;
};

} // namespace encore
