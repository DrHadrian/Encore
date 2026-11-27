// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Chain.h"
#include "engine/blocks/StudioEqBlock.h"
#include "model/ShowFolder.h"

#include <functional>
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

//== Gates (B2) ================================================================
/** What a patch's blocks learned while playing (a Smart gate's noise floor)
    carries over to the chain rebuilt from an edit, block by block id
    (GATE-PLAN 23). Message thread, before `rebuilt` plays; `playing` may be
    playing (its learned values are atomics). */
void carryLearnedState (const Chain& playing, Chain& rebuilt);

/** The Gate pair (GATE-PLAN 21): a Pedal Gate before the patch's first amp
    (Soft, Threshold 3) and one after it (Hard, Detect Guitar), linked to the
    first. Both in the amp's lane; `newId` names each. False, and nothing
    changes, if the patch has no amp. */
bool addGatePair (PatchModel& patch, const std::function<juce::String()>& newId);

/** Today's gate ("gate") as a Studio Gate (GATE-PLAN 11): Threshold and Range
    copy over, Release is converted so it closes along the same line (today's
    is a time constant), Detect guitar becomes Key Guitar or Input, the rest
    stays at the defaults (today's fixed values). Written for U2's switch-over;
    nothing calls it yet. A block of any other type comes back unchanged. */
BlockModel convertOldGate (const BlockModel& gate);

//== A / B (B3, MODULES-PLAN §3.1; every FX Unit) ==============================
/** Switches which set plays. The first switch makes B a copy of A. Only
    parameter values move. False (nothing changes) for a block that isn't an
    FX Unit. Message thread; one edit. */
bool switchCompare (BlockModel& block);

/** Copies A to B, or B to A. False for a block that isn't an FX Unit.
    Message thread; one edit. */
bool copyCompare (BlockModel& block, bool aToB);

/** Sets a live block's parameters from the model's (what A/B and presets
    change): heard at once, ramped as any change. Message thread. */
void applyParameters (const BlockModel& model, Block& block);

//== Studio EQ (B3) ============================================================
/** Adding a band to a Studio EQ's model (EQ-PLAN §2.8): it takes the lowest
    free slot, in use with these settings. Returns the slot (0-based), or -1
    and `error` ("All 24 bands are in use"). Message thread. */
int addEqBand (BlockModel& eq, const StudioEqBlock::BandSettings& band, juce::String& error);

/** Removing a band frees its slot: its parameters go back to their defaults
    and every expression assignment on them goes (and their scene marks once
    scenes exist), in the same edit. Returns how many assignments went.
    Message thread. */
int removeEqBand (BlockModel& eq, int slot);

/** Sets a live Studio EQ's slot from the model (the edit above, heard at
    once: the band glides in or out). Message thread. */
void applyEqSlot (const BlockModel& eq, Block& block, int slot);

/** Today's EQ ("eq") as a Studio EQ (EQ-PLAN §5), written for U3's
    switch-over; nothing calls it yet. Into slots 01, 02 ... in this order:
    lowCut above 20.5 Hz -> a Low cut, 12 dB/oct; bass -> a Low shelf at
    120 Hz, 12 dB/oct, Q 0.7071 (RBJ's S = 1); mid -> a Bell at midFreq,
    midQ; treble -> a High shelf at 3.5 kHz, likewise; highCut below
    19.9 kHz -> a High cut, 12 dB/oct. A gain counts when it's more than
    0.05 dB from 0 (as today's EQ decides what plays), or when it's on an
    expression pedal (so the pedal keeps working from its off value). On/off,
    the stomp and the id stay; expression moves to the bands' ids. A block of
    any other type comes back unchanged. */
BlockModel convertOldEq (const BlockModel& eq);

} // namespace encore
