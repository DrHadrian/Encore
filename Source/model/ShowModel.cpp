// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/ShowModel.h"

namespace encore
{

juce::String newId()
{
    return juce::Uuid().toDashedString();
}

ShowWords wordsFor (ShowTemplate t)
{
    if (t == ShowTemplate::musical)
        return { "Cue list", "cue", "cues", "scene", "New scene" };

    return { "Set list", "song", "songs", "set", "New set" };
}

juce::String describe (ShowTemplate t)
{
    switch (t)
    {
        case ShowTemplate::musical:  return "Musical / theatre";
        case ShowTemplate::worship:  return "Worship";
        case ShowTemplate::solo:     return "Solo";
        case ShowTemplate::band:     break;
    }

    return "Band";
}

RunItem RunItem::makeSection (const juce::String& name, const juce::String& sectionKind)
{
    RunItem item;
    item.kind = Kind::section;
    item.id = newId();
    item.name = name;
    item.sectionKind = sectionKind;
    return item;
}

RunItem RunItem::makeCue (const juce::String& number, const juce::String& name, const juce::String& patchId)
{
    RunItem item;
    item.kind = Kind::cue;
    item.id = newId();
    item.number = number;
    item.name = name;
    item.patchId = patchId;
    return item;
}

PatchModel* ShowModel::findPatch (const juce::String& patchId)
{
    for (auto& p : patches)
        if (p.id == patchId)
            return &p;

    return nullptr;
}

const PatchModel* ShowModel::findPatch (const juce::String& patchId) const
{
    return const_cast<ShowModel*> (this)->findPatch (patchId);
}

PatchModel makeDefaultPatch (const juce::String& name)
{
    PatchModel patch;
    patch.id = newId();
    patch.name = name;

    auto block = [] (const char* type, std::map<juce::String, float> params = {})
    {
        BlockModel b;
        b.id = newId();
        b.type = type;
        b.params = std::move (params);
        return b;
    };

    auto& lane = patch.mainLane();
    lane.blocks.push_back (block ("nam"));
    lane.blocks.push_back (block ("cab"));
    lane.blocks.push_back (block ("gate", { { "detectGuitar", 1.0f } }));
    lane.blocks.push_back (block ("eq"));
    return patch;
}

ShowModel makeDefaultShow (const juce::String& name)
{
    ShowModel show;
    show.id = newId();
    show.name = name;
    show.patches.push_back (makeDefaultPatch ("Clean"));
    return show;
}

PatchModel duplicatePatch (const PatchModel& original, const juce::String& newName)
{
    auto copy = original;
    copy.id = newId();
    copy.name = newName;

    for (auto& lane : copy.lanes)
        for (auto& b : lane.blocks)
            b.id = newId();

    return copy;
}

} // namespace encore
