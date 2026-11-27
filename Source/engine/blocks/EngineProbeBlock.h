// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/dsp/Crossfade.h"
#include "engine/dsp/FilterBand.h"

namespace encore
{

/** The Encore Engine's test unit: a hidden block that exercises the shared
    module base (native modules stage B0, decision 0062) end to end, so the
    tests, EncoreRender and the CPU benchmark have a real block to run before
    the first FX Unit exists.

    It is never offered in the Add block menu (BlockType::hidden). Shows,
    tests and EncoreRender can still make it.

    Signal: one filter band (dsp::FilterBand: every shape, decramped bells
    and shelves, cuts to 96 dB/oct), then a trim. Shape and Slope are choices:
    a change crossfades 10 ms from the old setting to the new (dsp::Crossfade).
    Frequency, gain and Q ramp; the band is redesigned every 16 samples while
    they move. With Shape "Off" (or a bell at 0 dB) and the trim at 0 dB it is
    bit-exact passthrough.

    Threads: as every block (Block): prepare off the audio thread (it sizes
    the crossfade buffer), process and reset on it, no allocation or locks
    there; designing a band is plain maths.
*/
class EngineProbeBlock final : public Block
{
public:
    static constexpr auto typeId = "engineProbe";
    static constexpr int updateInterval = 16;   // samples between redesigns while a value moves

    EngineProbeBlock();

    /** The band as it is set now (message thread, e.g. for a curve). */
    dsp::FilterSettings getFilterSettings() const noexcept;

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    dsp::FilterSettings settingsFrom (int shapeIndex, int slopeIndex, float f, float g, float q) const noexcept;

    Parameter& shape;
    Parameter& frequency;
    Parameter& gain;
    Parameter& q;
    Parameter& slope;
    Parameter& trim;

    double sampleRate = 48000.0;
    dsp::FilterBand<maxChannels> band, oldBand;
    dsp::Crossfade fade;
    int appliedShape = -1, appliedSlope = -1;
    dsp::FilterSettings appliedSettings;
};

} // namespace encore
