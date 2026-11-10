// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/dsp/Biquad.h"

namespace encore
{

/** A guitar EQ: low cut, bass shelf, sweepable mid, treble shelf, high cut.

    Coefficients are recalculated on the audio thread every 32 samples while a
    knob is moving (and not at all when nothing changes), without allocating.
*/
class EqBlock final : public Block
{
public:
    static constexpr auto typeId = "eq";

    EqBlock();

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    void updateCoefficients() noexcept;
    bool anyParameterSmoothing() const noexcept;

    Parameter& lowCut;
    Parameter& bassGain;
    Parameter& midGain;
    Parameter& midFrequency;
    Parameter& midQ;
    Parameter& trebleGain;
    Parameter& highCut;

    double sampleRate = 48000.0;
    bool coefficientsValid = false;

    enum Stage { lowCutStage, bassStage, midStage, trebleStage, highCutStage, numStages };
    std::array<dsp::Biquad<maxChannels>, numStages> stages;
    std::array<bool, numStages> stageActive {};
};

} // namespace encore
