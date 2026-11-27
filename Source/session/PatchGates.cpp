// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The gates' patch helpers (B2, declared in PatchBuilder.h): kept apart from
// the builder, which needs the plugin host, so they stand on their own.

#include "session/PatchBuilder.h"

#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/blocks/PedalGateBlock.h"
#include "engine/blocks/StudioGateBlock.h"

#include <cmath>

namespace encore
{

void carryLearnedState (const Chain& playing, Chain& rebuilt)
{
    for (const auto& block : rebuilt.getBlocks())
    {
        auto* gate = dynamic_cast<GateBlock*> (block.get());

        if (gate == nullptr || block->getModelId().isEmpty())
            continue;

        const auto at = playing.indexOfModelId (block->getModelId());

        if (at < 0)
            continue;

        if (auto* before = dynamic_cast<const GateBlock*> (playing.getBlocks()[(size_t) at].get()))
            gate->setLearnedFloorDb (before->getLearnedFloorDb());
    }
}

bool addGatePair (PatchModel& patch, const std::function<juce::String()>& newId)
{
    // The first amp in signal order: the main lane up to the merge (path A),
    // then path B, then the main lane after the merge.
    std::vector<BlockModel>* lane = nullptr;
    int at = -1;
    auto& main = patch.mainLane().blocks;
    const auto merge = patch.mergeIndex();
    const auto firstAmpIn = [] (const std::vector<BlockModel>& blocks, int from, int to)
    {
        for (int i = from; i < to; ++i)
            if (blocks[(size_t) i].type == NamAmpBlock::typeId)
                return i;

        return -1;
    };

    const auto beforeMerge = merge >= 0 ? merge : (int) main.size();

    if ((at = firstAmpIn (main, 0, beforeMerge)) >= 0)
        lane = &main;
    else if (auto* b = patch.pathB(); b != nullptr && (at = firstAmpIn (b->blocks, 0, (int) b->blocks.size())) >= 0)
        lane = &b->blocks;
    else if (merge >= 0 && (at = firstAmpIn (main, merge + 1, (int) main.size())) >= 0)
        lane = &main;

    if (lane == nullptr)
        return false;

    BlockModel before;
    before.id = newId();
    before.type = PedalGateBlock::typeId;
    before.params["soft"] = (float) PedalGateBlock::Mode::soft;
    before.params["threshold"] = 3.0f;

    BlockModel after;
    after.id = newId();
    after.type = PedalGateBlock::typeId;
    after.params["soft"] = (float) PedalGateBlock::Mode::hard;
    after.params["detect"] = (float) PedalGateBlock::Detect::guitar;
    after.refs["linkTo"] = before.id;

    lane->insert (lane->begin() + at + 1, after);
    lane->insert (lane->begin() + at, before);
    return true;
}

BlockModel convertOldGate (const BlockModel& gate)
{
    if (gate.type != NoiseGateBlock::typeId)
        return gate;

    auto converted = gate;   // id, on/off, stomp, everything else stays
    converted.type = StudioGateBlock::typeId;
    converted.params.clear();

    const auto valueOr = [&gate] (const char* id, float fallback)
    {
        const auto it = gate.params.find (id);
        return it != gate.params.end() ? it->second : fallback;
    };

    // Threshold and Range copy over. Today's Release is a one-pole time
    // constant: it falls 8.7 dB per release time, a straight line in dB. The
    // X4's falls the whole Range in its release time: so the same line is
    // release * |Range| / 8.7 dB (Adrian, 27 Nov), within the X4's 5-2000 ms.
    const auto range = valueOr ("range", -90.0f);
    const auto release = valueOr ("release", 80.0f);
    const auto dbPerTimeConstant = 20.0f * std::log10 (juce::MathConstants<float>::euler);
    converted.params["threshold"] = valueOr ("threshold", -60.0f);
    converted.params["range"] = range;
    converted.params["release"] = juce::jlimit (5.0f, 2000.0f, release * std::abs (range) / dbPerTimeConstant);

    // Today's gate defaults to Detect guitar on, as the X4's Key does.
    const auto it = gate.params.find ("detectGuitar");
    const auto guitar = it == gate.params.end() || it->second >= 0.5f;
    converted.params["key"] = (float) (guitar ? StudioGateBlock::Key::guitar : StudioGateBlock::Key::input);
    return converted;
}

} // namespace encore
