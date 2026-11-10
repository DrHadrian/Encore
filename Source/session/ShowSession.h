// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_events/juce_events.h>

#include "engine/AudioEngine.h"
#include "engine/core/BackgroundJobs.h"
#include "model/ShowFolder.h"

namespace encore
{

/** The open show, and everything that keeps it, the disk and the audio in step.

    - Holds the show (the saved model) and its folder.
    - Builds the selected patch into a chain on a background thread and hands
      it to the engine when it's ready.
    - Edits the active patch (add, remove, move blocks; patches in the library).
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

    //== Patches ===============================================================
    const juce::String& getActivePatchId() const noexcept { return activePatchId; }
    void selectPatch (const juce::String& patchId);

    juce::String addPatch (const juce::String& name);
    juce::String duplicatePatch (const juce::String& patchId);
    void renamePatch (const juce::String& patchId, const juce::String& newName);
    bool deletePatch (const juce::String& patchId);   // refuses to delete the last one

    /** True while the selected patch is being built (models loading). */
    bool isBuilding() const noexcept { return building; }

    //== Blocks in the active patch ============================================
    void addBlock (const juce::String& typeId, int index);
    void removeBlock (int index);
    void moveBlock (int from, int to);

    /** Call when a knob, switch or file changed in the live chain. */
    void markEdited();

private:
    void timerCallback() override;
    void changed (bool showContentChanged);
    void captureActive();
    void rebuildActiveChain();
    void adoptShow (ShowModel newShow, const ShowFolder& newFolder);
    juce::Result writeToDisk (bool keepBackup);
    PatchModel* activePatch();

    AudioEngine& engine;
    ShowModel show;
    ShowFolder folder;
    juce::String activePatchId;

    Chain* liveChain = nullptr;          // the chain we last handed to the engine
    juce::String liveChainPatchId;       // ...and which patch it was built from
    int buildGeneration = 0;
    bool building = false;

    bool dirty = false;
    juce::Time lastEdit, lastSave, lastBackup;
    juce::StringArray problems;

    juce::SharedResourcePointer<BackgroundJobs> jobs;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ShowSession)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShowSession)
};

} // namespace encore
