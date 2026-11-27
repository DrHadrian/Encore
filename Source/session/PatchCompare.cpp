// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// A / B for every FX Unit (B3, declared in PatchBuilder.h), apart from the
// builder so it stands on its own.

#include "session/PatchBuilder.h"

namespace encore
{

bool switchCompare (BlockModel& block)
{
    if (! isNewModuleBlockType (block.type))
        return false;

    if (! block.compare.isUsed())
        block.compare.other = block.params;   // B starts as a copy of A

    std::swap (block.params, block.compare.other);
    block.compare.bPlaying = ! block.compare.bPlaying;
    return true;
}

bool copyCompare (BlockModel& block, bool aToB)
{
    if (! isNewModuleBlockType (block.type))
        return false;

    if (! block.compare.isUsed())
        block.compare.other = block.params;

    auto& a = block.compare.bPlaying ? block.compare.other : block.params;
    auto& b = block.compare.bPlaying ? block.params : block.compare.other;

    if (aToB)
        b = a;
    else
        a = b;

    return true;
}

void applyParameters (const BlockModel& model, Block& block)
{
    for (const auto& p : block.getParameters())
    {
        const auto it = model.params.find (p->getSpec().id);
        p->setValue (it != model.params.end() ? it->second : p->getSpec().defaultValue);
    }
}

} // namespace encore
