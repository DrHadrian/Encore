// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_events/juce_events.h>
#include <map>

#include "engine/AudioEngine.h"
#include "engine/core/BackgroundJobs.h"
#include "model/ShowFolder.h"

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

    //== Blocks in the active patch ============================================
    void addBlock (const juce::String& typeId, int index);
    void removeBlock (int index);
    void moveBlock (int from, int to);

    /** Call when a knob, switch or file changed in the live chain. */
    void markEdited();

    /** Which stomp switch (1..8, 0 = none) toggles a block of the active patch. Instant. */
    void setBlockStomp (int blockIndex, int stomp);
    int getBlockStomp (int blockIndex) const;

    /** Puts a knob of the active patch on an expression pedal (1..4) over its
        full range, or takes it off (0). Rebuilds the patch (gapless). */
    void setExpression (int blockIndex, const juce::String& paramId, int pedal);
    int getExpressionPedal (int blockIndex, const juce::String& paramId) const;

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
    void publishRunningOrder();
    void captureAll();

    PatchModel* findPatchForKey (const juce::String& key);
    const PatchModel* findPatchForKey (const juce::String& key) const;
    static juce::String keyForCue (const RunItem& cue);
    RunItem* cueAt (int index);
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

    JUCE_DECLARE_WEAK_REFERENCEABLE (ShowSession)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowSession)
};

} // namespace encore
