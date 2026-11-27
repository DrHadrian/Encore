// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/FactoryPresets.h"

#include "engine/core/Block.h"

namespace encore::presets
{

const FactoryPreset* find (const Block& block, const juce::String& id) noexcept
{
    for (const auto& preset : block.getFactoryPresets())
        if (id == preset.id)
            return &preset;

    return nullptr;
}

std::map<juce::String, float> valuesFor (const Block& block, const FactoryPreset& preset)
{
    std::map<juce::String, float> values;

    for (const auto& p : block.getParameters())
        if (block.isPresetParameter (*p))
            values[p->getSpec().id] = p->getSpec().defaultValue;

    for (const auto& v : preset.values)
        if (auto* p = block.findParameter (v.parameterId); p != nullptr && block.isPresetParameter (*p))
            values[p->getSpec().id] = p->getSpec().range.snapToLegalValue (v.value);

    return values;
}

void apply (Block& block, const FactoryPreset& preset)
{
    for (const auto& [id, value] : valuesFor (block, preset))
        if (auto* p = block.findParameter (id))
            p->setValue (value);
}

juce::String listIds (const Block& block)
{
    juce::StringArray ids;

    for (const auto& preset : block.getFactoryPresets())
        ids.add (preset.id);

    return ids.isEmpty() ? juce::String ("none") : ids.joinIntoString (", ");
}

} // namespace encore::presets
