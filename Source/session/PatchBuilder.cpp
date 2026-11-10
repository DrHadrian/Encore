// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/PatchBuilder.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/EqBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"

namespace encore
{

const std::vector<BlockType>& getBlockTypes()
{
    static const std::vector<BlockType> types {
        { NamAmpBlock::typeId,    "Amp (NAM)" },
        { CabIrBlock::typeId,     "Cab (IR)" },
        { NoiseGateBlock::typeId, "Noise gate" },
        { EqBlock::typeId,        "EQ" },
    };
    return types;
}

std::unique_ptr<Block> createBlock (const juce::String& typeId)
{
    if (typeId == NamAmpBlock::typeId)    return std::make_unique<NamAmpBlock>();
    if (typeId == CabIrBlock::typeId)     return std::make_unique<CabIrBlock>();
    if (typeId == NoiseGateBlock::typeId) return std::make_unique<NoiseGateBlock>();
    if (typeId == EqBlock::typeId)        return std::make_unique<EqBlock>();
    return nullptr;
}

std::unique_ptr<Chain> buildChain (const PatchModel& patch, const ShowFolder& folder,
                                   const ProcessSpec& spec, juce::StringArray& problems)
{
    auto chain = std::make_unique<Chain>();

    for (const auto& model : patch.mainLane().blocks)
    {
        auto block = createBlock (model.type);

        if (block == nullptr)
        {
            problems.add ("Unknown block type \"" + model.type + "\" in " + patch.name + " (skipped)");
            continue;
        }

        block->setModelId (model.id);
        block->setEnabled (model.enabled);

        for (const auto& [paramId, value] : model.params)
            if (auto* p = block->findParameter (paramId))
                p->setValue (value);

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

        chain->addBlock (std::move (block));
    }

    if (spec.isValid())
        chain->prepare (spec);

    return chain;
}

void captureChain (const Chain& chain, PatchModel& patch, const ShowFolder& folder)
{
    std::vector<BlockModel> blocks;

    for (const auto& block : chain.getBlocks())
    {
        BlockModel model;
        model.id = block->getModelId().isNotEmpty() ? block->getModelId() : newId();
        model.type = block->getTypeId();
        model.enabled = block->isEnabled();

        for (const auto& p : block->getParameters())
            model.params[p->getSpec().id] = p->getValue();

        if (const auto file = block->getLoadedFile(); file != juce::File())
            model.file = folder.isValid() && file.isAChildOf (folder.getFolder())
                            ? file.getRelativePathFrom (folder.getFolder()).replaceCharacter ('\\', '/')
                            : file.getFullPathName();
        else
            model.file = block->getRequestedFile();   // couldn't load: keep what was asked for

        blocks.push_back (std::move (model));
    }

    patch.mainLane().blocks = std::move (blocks);
}

} // namespace encore
