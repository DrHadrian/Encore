// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Chain.h"
#include "model/ShowFolder.h"

#include <memory>

namespace encore
{

/** The block types Encore can build, in the order the "Add block" menu shows them. */
struct BlockType
{
    juce::String id;
    juce::String displayName;

    /** Not offered in the Add block menu (a module still being built, or the
        engine's test unit). createBlock, shows, the tests and EncoreRender
        still make it (decision 0062). */
    bool hidden = false;

    /** A native module that an older Encore doesn't know: a show that uses it
        is saved as schema 5, so an older Encore refuses the show instead of
        skipping the block (decision 0062; ShowModel::requiredSchemaVersion). */
    bool newModule = false;
};

/** Every type Encore can build, hidden ones included. */
const std::vector<BlockType>& getBlockTypes();

/** The types the Add block menu offers: getBlockTypes() without the hidden ones. */
std::vector<BlockType> getMenuBlockTypes();

/** A new block of the given type, or null if the type is unknown. */
std::unique_ptr<Block> createBlock (const juce::String& typeId);

class PluginCatalog;

/** What building a chain needs for plugin blocks. */
struct PluginContext
{
    std::shared_ptr<PluginCatalog> catalog;        // null: plugin blocks pass the signal through
    std::function<bool()> keepWaiting;             // false: give up (the show was closed)
    juce::StringArray crashedLastTime;             // block IDs to start switched off
};

/** Turns a saved patch into a chain that is ready to play the moment it is
    handed to the audio thread: every block created and set up, every amp
    model and cab IR loaded, and (if the device is known) prepared.

    Slow (loading models takes up to a second): call it on a background thread.
    Problems (a missing file, an unknown block type) don't stop the build; the
    affected block passes the signal through and the problem is reported.
*/
std::unique_ptr<Chain> buildChain (const PatchModel& patch, const ShowFolder& folder,
                                   const ProcessSpec& spec, juce::StringArray& problems,
                                   const PluginContext& plugins = {});

/** A plugin block for the show model's block (message thread, or waits for
    it). Never null: if the plugin can't be loaded the block passes the
    signal through, and `error` says why. */
std::unique_ptr<Block> createPluginBlock (const BlockModel& model, const ProcessSpec& spec,
                                          const PluginContext& plugins, juce::String& error);

/** Writes a playing chain's current state (block order, on/off, every
    parameter, loaded files, plugin settings) back into the patch it came
    from. Message thread. Files inside the show folder are stored as
    show-relative paths. */
void captureChain (const Chain& chain, PatchModel& patch, const ShowFolder& folder);

} // namespace encore
