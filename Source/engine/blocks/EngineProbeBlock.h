// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"

namespace encore
{

/** The Encore Engine's test unit: a hidden block that exercises the shared
    module base (native modules stage B0, decision 0062) end to end, so the
    tests, EncoreRender and the CPU benchmark have a real block to run before
    the first FX Unit exists.

    It is never offered in the Add block menu (BlockType::hidden). Shows,
    tests and EncoreRender can still make it.

    Today: a trim (dB). At 0 dB it is bit-exact passthrough.

    Threads: as every block (Block): prepare off the audio thread, process
    and reset on it, no allocation or locks there.
*/
class EngineProbeBlock final : public Block
{
public:
    static constexpr auto typeId = "engineProbe";

    EngineProbeBlock();

private:
    void prepareBlock (const ProcessSpec&) override {}
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override {}

    Parameter& trim;
};

} // namespace encore
