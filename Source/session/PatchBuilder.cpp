// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/PatchBuilder.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/DelayBlock.h"
#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/blocks/PluginBlock.h"
#include "engine/blocks/SplitBlocks.h"
#include "engine/core/MessageThread.h"
#include "session/PluginCatalog.h"

namespace encore
{

const std::vector<BlockType>& getBlockTypes()
{
    static const std::vector<BlockType> types {
        { NamAmpBlock::typeId,    "Amp (NAM)" },
        { CabIrBlock::typeId,     "Cab (IR)" },
        { NoiseGateBlock::typeId, "Noise gate" },
        { EqBlock::typeId,        "EQ" },
        { DelayBlock::typeId,     "Delay" },
    };
    return types;
}

std::unique_ptr<Block> createBlock (const juce::String& typeId)
{
    if (typeId == NamAmpBlock::typeId)    return std::make_unique<NamAmpBlock>();
    if (typeId == CabIrBlock::typeId)     return std::make_unique<CabIrBlock>();
    if (typeId == NoiseGateBlock::typeId) return std::make_unique<NoiseGateBlock>();
    if (typeId == EqBlock::typeId)        return std::make_unique<EqBlock>();
    if (typeId == DelayBlock::typeId)     return std::make_unique<DelayBlock>();
    if (typeId == SplitBlock::typeId)     return std::make_unique<SplitBlock>();
    if (typeId == MergeBlock::typeId)     return std::make_unique<MergeBlock>();
    return nullptr;
}

std::unique_ptr<Block> createPluginBlock (const BlockModel& model, const ProcessSpec& spec,
                                          const PluginContext& plugins, juce::String& error)
{
    std::unique_ptr<Block> block;
    const auto& ref = model.plugin;
    const auto name = ref.name.isNotEmpty() ? ref.name : juce::String ("A plugin");

    // Created, set up and wrapped on the message thread (VST3 plugins expect it).
    const auto done = runOnMessageThread ([&]
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;

        if (plugins.catalog == nullptr)
            error = name + " can't be loaded here";
        else if (const auto description = plugins.catalog->find (ref))
            instance = plugins.catalog->createInstance (*description,
                                                        spec.isValid() ? spec.sampleRate : 48000.0,
                                                        spec.isValid() ? spec.maxBlockSize : 512, error);
        else if (const auto blocked = plugins.catalog->findBlocked (ref))
            error = name + " is blocked (" + blocked->reason.toLowerCase() + ")";
        else
            error = name + " isn't installed on this computer";

        if (instance != nullptr)
            PluginBlock::setUp (*instance, model.state);
        else if (error.isEmpty())
            error = name + " couldn't be loaded";

        block = std::make_unique<PluginBlock> (ref, std::move (instance), model.state, model.id);
    }, plugins.keepWaiting);

    if (! done)
    {
        error = "Cancelled";
        block = std::make_unique<PluginBlock> (ref, nullptr, model.state, model.id);
    }

    auto& plugin = static_cast<PluginBlock&> (*block);
    plugin.setKeepWaiting (plugins.keepWaiting);
    plugin.setLoadError (error);

    return block;
}

std::unique_ptr<Chain> buildChain (const PatchModel& patch, const ShowFolder& folder,
                                   const ProcessSpec& spec, juce::StringArray& problems,
                                   const PluginContext& plugins)
{
    auto chain = std::make_unique<Chain>();

    // The main lane, then path B (only with a split: the model keeps them so).
    std::vector<std::pair<const BlockModel*, bool>> models;

    for (const auto& model : patch.mainLane().blocks)
        models.push_back ({ &model, false });

    if (const auto* pathB = patch.pathB())
        for (const auto& model : pathB->blocks)
            models.push_back ({ &model, true });

    for (const auto& [modelPointer, toPathB] : models)
    {
        const auto& model = *modelPointer;
        std::unique_ptr<Block> block;

        if (model.type == PluginBlock::typeId)
        {
            juce::String error;
            block = createPluginBlock (model, spec, plugins, error);

            if (error.isNotEmpty())
                problems.add (patch.name + ": " + error + " (the signal passes through)");

            if (plugins.crashedLastTime.contains (model.id))
                static_cast<PluginBlock&> (*block).markCrashedLastTime();
        }
        else
        {
            block = createBlock (model.type);
        }

        if (block == nullptr)
        {
            problems.add ("Unknown block type \"" + model.type + "\" in " + patch.name + " (skipped)");
            continue;
        }

        block->setModelId (model.id);
        block->setEnabled (model.enabled);
        block->setStomp (model.stomp);

        for (const auto& [paramId, value] : model.params)
            if (auto* p = block->findParameter (paramId))
                p->setValue (value);

        for (const auto& [paramId, assignment] : model.expression)
            if (auto* p = block->findParameter (paramId))
                chain->addExpressionTarget (assignment.pedal, *p, assignment.min, assignment.max);

        if (model.file.isNotEmpty())
        {
            block->setRequestedFile (model.file);
            const auto file = folder.resolve (model.file);
            juce::String error;

            if (auto* amp = dynamic_cast<NamAmpBlock*> (block.get()))
            {
                if (auto loaded = NamModel::load (file, spec, error))
                    amp->installBeforePlaying (std::move (loaded));
            }
            else if (auto* cab = dynamic_cast<CabIrBlock*> (block.get()))
            {
                if (auto ir = CabIrBlock::readFile (file, error))
                    cab->loadBeforePlaying (std::move (ir));
            }

            if (error.isNotEmpty())
                problems.add (patch.name + ": " + error);
        }

        chain->addBlock (std::move (block), toPathB);
    }

    if (spec.isValid())
        chain->prepare (spec);

    return chain;
}

void captureChain (const Chain& chain, PatchModel& patch, const ShowFolder& folder)
{
    std::vector<BlockModel> blocks, pathBBlocks;

    auto findPrevious = [&patch] (const juce::String& id) -> const BlockModel*
    {
        for (auto& lane : patch.lanes)
            for (auto& b : lane.blocks)
                if (b.id == id)
                    return &b;

        return nullptr;
    };

    const auto& chainBlocks = chain.getBlocks();

    for (size_t index = 0; index < chainBlocks.size(); ++index)
    {
        const auto& block = chainBlocks[index];
        auto& into = (int) index < chain.getNumMainBlocks() ? blocks : pathBBlocks;
        BlockModel model;
        model.id = block->getModelId().isNotEmpty() ? block->getModelId() : newId();
        model.type = block->getTypeId();
        model.enabled = block->isEnabled();
        model.stomp = block->getStomp();

        const auto* previous = findPrevious (model.id);

        if (previous != nullptr)
            model.expression = previous->expression;   // not represented live: carry it over

        if (auto* plugin = dynamic_cast<PluginBlock*> (block.get()))
        {
            // The plugin's own state holds its settings. Only knobs on a pedal
            // are kept apart, at their saved value (as below).
            model.plugin = plugin->getPluginRef();
            model.state = plugin->getState();

            for (const auto& [id, assignment] : model.expression)
            {
                if (previous != nullptr && previous->params.count (id) > 0)
                    model.params[id] = previous->params.at (id);
                else if (auto* p = plugin->findParameter (id))
                    model.params[id] = p->getValue();
            }

            into.push_back (std::move (model));
            continue;
        }

        for (const auto& p : block->getParameters())
        {
            const auto& id = p->getSpec().id;

            // A knob on an expression pedal is wherever the pedal happens to be:
            // keep its saved value instead.
            if (previous != nullptr && model.expression.count (id) > 0 && previous->params.count (id) > 0)
                model.params[id] = previous->params.at (id);
            else
                model.params[id] = p->getValue();
        }

        if (const auto file = block->getLoadedFile(); file != juce::File())
            model.file = folder.isValid() && file.isAChildOf (folder.getFolder())
                            ? file.getRelativePathFrom (folder.getFolder()).replaceCharacter ('\\', '/')
                            : file.getFullPathName();
        else
            model.file = block->getRequestedFile();   // couldn't load: keep what was asked for

        into.push_back (std::move (model));
    }

    // The chain may be older than the patch (an edit whose rebuild hasn't
    // arrived yet): then only take the values of the blocks both have, and
    // keep the patch's own order, splits and new blocks.
    auto ids = [] (const std::vector<BlockModel>& list)
    {
        juce::StringArray result;

        for (auto& b : list)
            result.add (b.id);

        return result;
    };

    const auto* oldPathB = patch.lanes.size() > 1 ? &patch.lanes[1].blocks : nullptr;
    const auto sameShape = ids (blocks) == ids (patch.mainLane().blocks)
                           && ids (pathBBlocks) == (oldPathB != nullptr ? ids (*oldPathB) : juce::StringArray());

    if (sameShape)
    {
        patch.mainLane().blocks = std::move (blocks);

        if (patch.lanes.size() > 1)
            patch.lanes[1].blocks = std::move (pathBBlocks);

        return;
    }

    for (auto& lane : patch.lanes)
        for (auto& b : lane.blocks)
            for (auto* captured : { &blocks, &pathBBlocks })
                for (auto& c : *captured)
                    if (c.id == b.id)
                        b = c;
}

} // namespace encore
