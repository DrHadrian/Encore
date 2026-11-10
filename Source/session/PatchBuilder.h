// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Chain.h"
#include "model/ShowFolder.h"

namespace encore
{

/** The block types Encore can build, in the order the "Add block" menu shows them. */
struct BlockType
{
    juce::String id;
    juce::String displayName;
};

const std::vector<BlockType>& getBlockTypes();

/** A new block of the given type, or null if the type is unknown. */
std::unique_ptr<Block> createBlock (const juce::String& typeId);

/** Turns a saved patch into a chain that is ready to play the moment it is
    handed to the audio thread: every block created and set up, every amp
    model and cab IR loaded, and (if the device is known) prepared.

    Slow (loading models takes up to a second): call it on a background thread.
    Problems (a missing file, an unknown block type) don't stop the build; the
    affected block passes the signal through and the problem is reported.
*/
std::unique_ptr<Chain> buildChain (const PatchModel& patch, const ShowFolder& folder,
                                   const ProcessSpec& spec, juce::StringArray& problems);

/** Writes a playing chain's current state (block order, on/off, every
    parameter, loaded files) back into the patch it came from. Message thread.
    Files inside the show folder are stored as show-relative paths. */
void captureChain (const Chain& chain, PatchModel& patch, const ShowFolder& folder);

} // namespace encore
