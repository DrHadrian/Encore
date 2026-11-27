// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <span>

namespace encore
{

class Block;

/** One parameter's value in a factory preset. */
struct PresetValue
{
    const char* parameterId;
    float value;
};

/** A factory preset: compiled in, shared format for every FX Unit
    (MODULES-PLAN §3.1; decision 0065).

    - `id` is stable (shows and, later, user preset libraries refer to it):
      never renamed once merged. `name` is what players see.
    - A preset sets parameters only: never on/off, stomp, expression or a
      loaded file. Every parameter the block counts as part of a preset
      (Block::isPresetParameter) goes to its default, then to the values
      listed here, so a preset is the whole sound, not a change on top.
    - Picking one is one step for the undo system (presets::valuesFor gives
      every value at once).

    Plain data in static tables: any thread.
*/
struct FactoryPreset
{
    const char* id;
    const char* name;
    std::span<const PresetValue> values;
};

namespace presets
{
    /** The block's factory preset with this id, or null. Any thread. */
    const FactoryPreset* find (const Block& block, const juce::String& id) noexcept;

    /** Every value a preset sets on this block: its preset parameters at
        their defaults, then the preset's own values (clamped and snapped to
        each parameter's range). For a show model or an undo step.
        Message thread (builds a map). */
    std::map<juce::String, float> valuesFor (const Block& block, const FactoryPreset& preset);

    /** Sets the preset's values on a block (message thread, as any parameter
        change; the audio thread ramps or crossfades them). */
    void apply (Block& block, const FactoryPreset& preset);

    /** The ids, for listing and error messages ("flat, midPush, ..."). */
    juce::String listIds (const Block& block);
}

} // namespace encore
