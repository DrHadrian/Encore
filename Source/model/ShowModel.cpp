// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/ShowModel.h"
#include "model/Routing.h"

#include <algorithm>

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

namespace
{
    bool hasPlugin (const PatchModel& patch)
    {
        for (const auto& lane : patch.lanes)
            for (const auto& b : lane.blocks)
                if (b.type == pluginBlockType)
                    return true;

        return false;
    }

    bool hasPlugin (const RunItem& item)
    {
        if (item.uniquePatch && hasPlugin (*item.uniquePatch))
            return true;

        return std::any_of (item.children.begin(), item.children.end(), [] (const RunItem& c) { return hasPlugin (c); });
    }
}

bool ShowModel::usesPlugins() const
{
    return std::any_of (patches.begin(), patches.end(), [] (const PatchModel& p) { return hasPlugin (p); })
           || hasPlugin (runningOrder);
}

//==============================================================================
SendModel PatchModel::sendFor (const juce::String& outputName) const
{
    for (const auto& s : sends)
        if (routing::sameName (s.output, outputName))
            return s;

    return { outputName, 0.0f, true };
}

void PatchModel::setSend (const juce::String& outputName, float levelDb, bool on)
{
    for (auto& s : sends)
    {
        if (routing::sameName (s.output, outputName))
        {
            s.levelDb = routing::clampLevel (levelDb);
            s.on = on && levelDb > routing::silentDb;
            return;
        }
    }

    sends.push_back ({ outputName, routing::clampLevel (levelDb), on && levelDb > routing::silentDb });
}

namespace
{
    int indexOfType (const LaneModel& lane, const char* type)
    {
        for (size_t i = 0; i < lane.blocks.size(); ++i)
            if (lane.blocks[i].type == type)
                return (int) i;

        return -1;
    }

    bool hasType (const PatchModel& patch, const char* type)
    {
        for (const auto& lane : patch.lanes)
            if (indexOfType (lane, type) >= 0)
                return true;

        return false;
    }

    BlockModel makeBlock (const char* type)
    {
        BlockModel b;
        b.id = newId();
        b.type = type;
        return b;
    }

    BlockModel makeMerge()
    {
        auto b = makeBlock (mergeBlockType);
        b.params = { { mergeParams::levelA, 0.0f }, { mergeParams::panA, 0.0f },
                     { mergeParams::levelB, 0.0f }, { mergeParams::panB, 0.0f } };
        return b;
    }

    void collectNames (const PatchModel& patch, juce::StringArray& inputs, juce::StringArray& outputs)
    {
        const auto& input = patch.inputName();

        if (input.trim().isNotEmpty() && ! inputs.contains (input.trim(), true))
            inputs.add (input.trim());

        for (const auto& s : patch.sends)
            if (s.output.trim().isNotEmpty() && ! outputs.contains (s.output.trim(), true))
                outputs.add (s.output.trim());
    }

    void forEachOwnPatch (const RunItem& item, const std::function<void (const PatchModel&)>& fn)
    {
        if (item.uniquePatch)
            fn (*item.uniquePatch);

        for (const auto& c : item.children)
            forEachOwnPatch (c, fn);
    }

    void forEachOwnPatch (RunItem& item, const std::function<void (PatchModel&)>& fn)
    {
        if (item.uniquePatch)
            fn (*item.uniquePatch);

        for (auto& c : item.children)
            forEachOwnPatch (c, fn);
    }
}

int PatchModel::splitIndex() const  { return indexOfType (mainLane(), splitBlockType); }
int PatchModel::mergeIndex() const  { return indexOfType (mainLane(), mergeBlockType); }

bool PatchModel::usesRouting() const
{
    return inputName().trim().isNotEmpty() || ! sends.empty() || lanes.size() > 1
           || hasType (*this, splitBlockType) || hasType (*this, mergeBlockType);
}

void ShowModel::forEachPatch (const std::function<void (const PatchModel&)>& fn) const
{
    for (const auto& p : patches)
        fn (p);

    forEachOwnPatch (runningOrder, fn);
}

void ShowModel::forEachPatch (const std::function<void (PatchModel&)>& fn)
{
    for (auto& p : patches)
        fn (p);

    forEachOwnPatch (runningOrder, fn);
}

bool ShowModel::usesRouting() const
{
    bool uses = false;
    forEachPatch ([&] (const PatchModel& p) { uses = uses || p.usesRouting(); });
    return uses;
}

juce::StringArray ShowModel::inputNamesUsed() const
{
    juce::StringArray inputs, outputs;
    forEachPatch ([&] (const PatchModel& p) { collectNames (p, inputs, outputs); });
    return inputs;
}

juce::StringArray ShowModel::outputNamesUsed() const
{
    juce::StringArray inputs, outputs;
    forEachPatch ([&] (const PatchModel& p) { collectNames (p, inputs, outputs); });
    return outputs;
}

void normaliseSplit (PatchModel& patch)
{
    auto& main = patch.mainLane();
    const auto split = indexOfType (main, splitBlockType);

    // Blocks of the wrong kind in the wrong place: splits and merges only
    // belong in the main lane, once each, merge after split.
    auto removeAll = [] (LaneModel& lane, const char* type, int keep)
    {
        for (int i = (int) lane.blocks.size(); --i >= 0;)
            if (i != keep && lane.blocks[(size_t) i].type == type)
                lane.blocks.erase (lane.blocks.begin() + i);
    };

    for (size_t l = 1; l < patch.lanes.size(); ++l)
    {
        removeAll (patch.lanes[l], splitBlockType, -1);
        removeAll (patch.lanes[l], mergeBlockType, -1);
    }

    if (split < 0)
    {
        removeAll (main, mergeBlockType, -1);
        patch.lanes.resize (1);
        return;
    }

    removeAll (main, splitBlockType, split);

    int merge = -1;
    for (int i = (int) main.blocks.size(); --i > split;)
        if (main.blocks[(size_t) i].type == mergeBlockType)
            merge = i;   // the first merge after the split

    // Merges before the split, or more than one after it.
    for (int i = (int) main.blocks.size(); --i >= 0;)
        if (i != merge && main.blocks[(size_t) i].type == mergeBlockType)
            main.blocks.erase (main.blocks.begin() + i);

    if (indexOfType (main, mergeBlockType) < 0)
        main.blocks.push_back (makeMerge());

    patch.lanes.resize (2);
}

bool insertSplit (PatchModel& patch, int index)
{
    if (patch.hasSplit())
        return false;

    auto& main = patch.mainLane();
    index = juce::jlimit (0, (int) main.blocks.size(), index);
    main.blocks.insert (main.blocks.begin() + index, makeMerge());
    main.blocks.insert (main.blocks.begin() + index, makeBlock (splitBlockType));
    patch.lanes.resize (2);
    patch.lanes[1] = LaneModel{};
    return true;
}

void removeSplit (PatchModel& patch)
{
    auto& main = patch.mainLane();

    for (int i = (int) main.blocks.size(); --i >= 0;)
    {
        const auto& type = main.blocks[(size_t) i].type;

        if (type == splitBlockType || type == mergeBlockType)
            main.blocks.erase (main.blocks.begin() + i);
    }

    patch.lanes.resize (1);
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
