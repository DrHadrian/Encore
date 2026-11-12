// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_events/juce_events.h>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "engine/AudioEngine.h"
#include "engine/core/BackgroundJobs.h"
#include "model/ShowFolder.h"
#include "session/PatchBuilder.h"

namespace encore
{

/** The open show, and everything that keeps it, the disk and the audio in step.

    - Holds the show (the saved model) and its folder.
    - When a show opens, builds a chain for every patch on a background thread
      (the playing patch first), so switching never loads anything.
    - Publishes the running order to the engine's PatchPlayer, and follows
      what the audio thread actually plays (cues can change from MIDI later).
    - Edits patches, blocks and the running order; rebuilds only the patch
      whose block structure changed.
    - Writes live knob changes back into the show and autosaves: 2 s after the
      last change, keeping a backup of the previous version at most every
      5 minutes (and on every explicit save).

    Message thread only. Listeners get a change message whenever the show,
    the selection or the saved state changes.
*/
class ShowSession final : public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    explicit ShowSession (AudioEngine& engine);
    ~ShowSession() override;

    //== Shows =================================================================
    /** Creates a show folder with one default patch. starterFiles (block type
        -> file) pre-load e.g. the amp and cab you used last. */
    juce::Result newShow (const juce::File& folder, const juce::String& name,
                          const std::map<juce::String, juce::File>& starterFiles = {});
    juce::Result openShow (const juce::File& folderOrShowFile);

    /** Captures live changes and saves now, keeping a backup of the previous file. */
    juce::Result save();

    /** Saves, then copies the whole show folder into another folder (e.g. a USB
        stick). Keeps working on the original. Returns the copy's location. */
    juce::Result saveCopyTo (const juce::File& parentFolder, juce::File& copy);

    const ShowModel& getShow() const noexcept    { return show; }
    const ShowFolder& getFolder() const noexcept { return folder; }
    bool hasUnsavedChanges() const noexcept      { return dirty; }
    juce::Time getLastSaveTime() const noexcept  { return lastSave; }

    /** Things that went wrong while loading or saving (missing files...), oldest first. Clears them. */
    juce::StringArray takeProblems();

    /** True while patches are being built (show opening, or a patch being rebuilt). */
    bool isBuilding() const noexcept { return pendingBuilds > 0; }

    /** Call often (UI timer): follows the audio thread's patch and cue changes. */
    void syncWithAudio();

    //== Patches ===============================================================
    /** The patch playing now (the one the edit view shows). */
    const juce::String& getActivePatchId() const noexcept { return activePatchId; }

    /** Plays a patch directly (the cue position stays where it is). */
    void selectPatch (const juce::String& patchId);

    juce::String addPatch (const juce::String& name);
    juce::String duplicatePatch (const juce::String& patchId);
    void renamePatch (const juce::String& patchId, const juce::String& newName);
    bool deletePatch (const juce::String& patchId);   // refuses to delete the last one
    void setSpillover (const juce::String& patchId, Spillover mode);
    void setSwitchFades (const juce::String& patchId, float fadeInMs, float ringOutMs);

    /** How many cues use a patch. */
    int countCuesUsing (const juce::String& patchId) const;

    //== Plugins ===============================================================
    /** The plugins this computer has (null: plugin blocks pass the signal through). */
    void setPluginCatalog (std::shared_ptr<PluginCatalog>);
    const std::shared_ptr<PluginCatalog>& getPluginCatalog() const noexcept { return pluginCatalog; }

    /** Plugin blocks (by block ID) that were running when Encore crashed: they
        start switched off, with a notice, until turned back on. */
    void setCrashedPlugins (const juce::StringArray& blockIds);

    /** Tries a plugin block of the active patch that the crash guard switched off again. */
    void turnPluginBackOn (int blockIndex);

    /** Plugin blocks that aren't playing (not loaded, or switched off by the
        crash guard): "Lead: Shimmer isn't installed on this computer". */
    std::vector<juce::String> getPluginProblems() const;

    /** Plugins the crash guard just switched off ("Clean: Shimmer crashed and
        was switched off"), for a notice. Clears them. */
    juce::StringArray takePluginNotices();

    //== Blocks in the active patch ============================================
    /** Adds a block at index within a lane (0 = the main lane, 1 = path B). */
    void addBlock (const juce::String& typeId, int index, int lane = 0);
    void addPluginBlock (const PluginRef& plugin, int index, int lane = 0);

    /** Block indices below are the chain's: the main lane, then path B.
        Removing the split or the merge removes the split (path A's blocks
        stay, path B's go). A move stays within the block's lane and never
        passes the split or the merge. */
    void removeBlock (int index);
    void moveBlock (int from, int to);

    /** "Move to path A / B": from path A to the end of path B, or back to the
        end of path A. */
    void moveBlockToPath (int index, bool toPathB);

    /** Split paths (milestone 11): a split and a merge before the main lane's
        block at index, both paths empty. False if the patch has one already. */
    bool addSplit (int index);
    void removeSplit();

    //== Routing (milestone 11) ================================================
    /** A patch's input (by name, "" = this computer's first) and its send to
        an output. Both live at once, no rebuild. patchKey as getActivePatchId(). */
    void setPatchInput (const juce::String& patchKey, const juce::String& inputName);
    void setPatchSend (const juce::String& patchKey, const juce::String& outputName, float levelDb, bool on);

    /** Re-applies every patch's input and sends, after this computer's
        routing changed (AudioEngine::setRouting). */
    void refreshRouting();

    /** Patches whose split paths are too far apart to line up. */
    juce::StringArray getUnalignedPatches() const;

    /** The patch of a key (a library patch's id, or a cue's own), or null. */
    const PatchModel* getPatch (const juce::String& patchKey) const   { return findPatchForKey (patchKey); }

    /** Call when a knob, switch or file changed in the live chain. */
    void markEdited();

    /** Which stomp switch (1..8, 0 = none) toggles a block of the active patch. Instant. */
    void setBlockStomp (int blockIndex, int stomp);
    int getBlockStomp (int blockIndex) const;

    /** Puts a knob of the active patch on an expression pedal (1..4) over its
        full range, or takes it off (0). Rebuilds the patch (gapless). */
    void setExpression (int blockIndex, const juce::String& paramId, int pedal);
    int getExpressionPedal (int blockIndex, const juce::String& paramId) const;

    /** The pedal and its heel / toe values for a parameter, if one moves it. */
    std::optional<ExpressionAssignment> getExpressionAssignment (int blockIndex, const juce::String& paramId) const;

    //== Running order =========================================================
    /** The cues in playing order (sections flattened, cut cues left out). */
    std::vector<const RunItem*> getCues() const;

    /** The cue the audio thread is at, or -1. */
    int getCurrentCue() const noexcept { return currentCue; }

    void next();
    void previous();
    void goToCue (int index);

    /** Adds a cue for a patch after the given cue (or at the end). Returns its ID. */
    juce::String addCue (const juce::String& patchId, int afterIndex);
    void removeCue (int index);
    void moveCue (int index, int delta);
    void renameCue (int index, const juce::String& name);
    void setCuePatch (int index, const juce::String& patchId);
    void setCueSamePatch (int index, SamePatchBehaviour behaviour);
    void setCueProgramChange (int index, int program);   // -1 = none
    void setCueMidiOut (int index, std::vector<MidiOutMessage> messages);

    //== Running order editing (set lists and theatre cue lists) =============
    /** Band, musical, worship, solo: decides the words (set list / cue list). */
    void setShowTemplate (ShowTemplate);
    ShowWords getWords() const { return wordsFor (show.showTemplate); }

    /** Every item in order (sections, cues, cut cues), as the cue list shows them. */
    struct Row
    {
        const RunItem* item = nullptr;
        int depth = 0;          // 0 = top level, 1 = inside a section...
        int cueIndex = -1;      // position among playable cues; -1 for sections and cut cues
    };

    std::vector<Row> getRunningOrderRows() const;

    /** Adds a cue after an item (inside a section if `afterId` is a section;
        at the end if empty). It plays the same patch as the cue before it.
        Cues are renumbered 1..N. Returns the new cue's id. */
    juce::String insertCue (const juce::String& afterId);
    juce::String insertSection (const juce::String& afterId, const juce::String& name, const juce::String& kind);

    /** Removes an item. A section's cues stay, moving up a level. */
    void deleteItem (const juce::String& id);

    /** Moves an item one place up (-1) or down (+1) in the list; at a section's
        edge it moves out of it, next to a section it moves into it. */
    void moveItem (const juce::String& id, int direction);

    /** Cut cues stay in the file but are skipped in the show (and unnumbered). */
    void setCut (const juce::String& id, bool cut);

    void setItemName (const juce::String& id, const juce::String& name);
    void setItemNote (const juce::String& id, const juce::String& note);
    void setItemBarMark (const juce::String& id, const juce::String& barMark);
    void setSectionKind (const juce::String& id, const juce::String& kind);
    void setItemPatch (const juce::String& id, const juce::String& patchId);

    /** Playable cue index of an item, or -1. */
    int getCueIndexOf (const juce::String& id) const;

    /** "Go to cue 42": the playable cue with that number, or -1. */
    int findCueByNumber (const juce::String& number) const;

    /** "Act 1 / Scene 3" for a playable cue (empty at the top level). */
    juce::String getSectionPath (int cueIndex) const;

    //== MIDI mappings =========================================================
    /** Whether the show uses its own mappings instead of the computer's. */
    void setOwnMidiMappings (bool own, const std::vector<MidiBinding>& startingPoint);
    void setShowMidiBindings (std::vector<MidiBinding> bindings);

    //== Stage view ============================================================
    /** The layouts (at least the built-in default) and tuner/panic settings. */
    const StageSettings& getStage() const noexcept { return show.stage; }

    /** The layout for a cue: its own, else the show's default, else built-in. */
    const StageLayout& getLayoutForCue (int index) const;

    /** Adds a layout (a copy of `copyFrom`, or the default) and returns its id. */
    juce::String addLayout (const juce::String& name, const juce::String& copyFromId);
    void renameLayout (const juce::String& id, const juce::String& name);
    bool deleteLayout (const juce::String& id);   // false for the last one
    void setDefaultLayout (const juce::String& id);
    void setLayoutWidgets (const juce::String& id, std::vector<StageWidget> widgets);
    void setCueLayout (int index, const juce::String& layoutId);   // empty = default

    void setTunerMutes (bool mutes);
    void setReferencePitch (float hz);
    void setPanicBehaviour (PanicBehaviour);

private:
    void timerCallback() override;
    void applyStageSettings();
    void ensureLayouts();
    void changed (bool showContentChanged);
    void adoptShow (ShowModel newShow, const ShowFolder& newFolder);
    juce::Result writeToDisk (bool keepBackup);

    void buildAll();
    void rebuildPatch (const juce::String& patchId);
    void installChain (const juce::String& key, std::unique_ptr<Chain> chain);
    void applyRouting (Chain& chain, const PatchModel& patch) const;
    void publishRunningOrder();
    void captureAll();

    PatchModel* findPatchForKey (const juce::String& key);
    const PatchModel* findPatchForKey (const juce::String& key) const;
    static juce::String keyForCue (const RunItem& cue);
    RunItem* cueAt (int index);
    RunItem* findItem (const juce::String& id);
    void renumberCues();
    void runningOrderChanged (const juce::String& keepPlayingId);
    bool locateCue (int index, std::vector<RunItem>*& siblings, int& position);

    AudioEngine& engine;
    PatchPlayer& player;
    ShowModel show;
    ShowFolder folder;

    // One chain per patch (key = patch ID, or "cue:<id>" for a cue's own patch).
    std::map<juce::String, Chain*> chains;
    int showGeneration = 0;
    std::map<juce::String, int> buildGenerations;
    int pendingBuilds = 0;

    juce::String activePatchId;   // follows the audio thread
    int currentCue = -1;
    Chain* lastSeenActive = nullptr;

    bool dirty = false;
    juce::Time lastEdit, lastSave, lastBackup;
    juce::StringArray problems;

    juce::SharedResourcePointer<BackgroundJobs> jobs;

    // Plugins. `buildsWanted` changes when a show is replaced or the session
    // ends, which tells plugin creation still waiting to give up.
    PluginContext makePluginContext() const;
    void checkPluginFaults();
    std::shared_ptr<PluginCatalog> pluginCatalog;
    std::shared_ptr<std::atomic<int>> buildsWanted = std::make_shared<std::atomic<int>> (0);
    juce::StringArray crashedPlugins, pluginNotices;
    int lastFaultCount = 0;
    std::set<juce::String> reportedFaults;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ShowSession)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowSession)
};

} // namespace encore
