// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "model/ShowModel.h"

namespace encore
{

/** A show on disk: a self-contained folder you can copy to a USB stick and
    open on any computer.

        My Show.encore/
            show.json         the show (patches, running order)
            models/           every .nam capture the show uses (copies)
            irs/              every cab IR it uses (copies)
            backups/          earlier versions of show.json, newest 50 kept

    Saving is atomic: the new show.json is written to a temporary file, flushed
    to disk, then swapped in, so a crash or power cut mid-save leaves either
    the old or the new file, never a broken one.

    Message thread or background thread; never the audio thread.
*/
class ShowFolder
{
public:
    static constexpr auto showFileName = "show.json";
    static constexpr auto folderExtension = ".encore";
    static constexpr int maxBackups = 50;

    ShowFolder() = default;
    explicit ShowFolder (const juce::File& folder) : root (folder) {}

    /** Makes a new show folder (and its subfolders) and writes the show into it. */
    static juce::Result create (const juce::File& folder, ShowModel& show);

    /** Opens a show from its folder, or from the show.json inside it. */
    static std::optional<ShowModel> open (const juce::File& folderOrShowFile, ShowFolder& opened, juce::String& error);

    /** A folder name for a show ("My Show.encore") inside a parent folder. */
    static juce::File suggestFolder (const juce::File& parent, const juce::String& showName);

    bool isValid() const { return root != juce::File(); }
    const juce::File& getFolder() const noexcept { return root; }
    juce::File getShowFile() const     { return root.getChildFile (showFileName); }
    juce::File getBackupsFolder() const { return root.getChildFile ("backups"); }

    /** Copies every model/IR the show uses into the folder first (see
        collectAssets), then writes show.json. The result says whether the show
        file was written; files that couldn't be copied in are listed in
        assetProblems (the show is still saved, with their old paths). */
    juce::Result save (ShowModel& show, bool keepBackupOfPrevious, juce::StringArray* assetProblems = nullptr);

    /** Backups, newest first. */
    juce::Array<juce::File> listBackups() const;

    //== Assets ================================================================
    /** Copies a file into the folder's models/ or irs/ (unless it's already in
        the show) and returns its show-relative path. Identical files are
        shared; a different file with the same name gets a numbered name. */
    juce::String importAsset (const juce::File& source, const juce::String& subfolder, juce::String& error) const;

    /** Imports every block file that isn't inside the show yet and rewrites the
        paths to show-relative ones. Missing files are left as they are and listed. */
    juce::Result collectAssets (ShowModel& show) const;

    /** The absolute file for a stored path (relative paths resolve inside the folder). */
    juce::File resolve (const juce::String& storedPath) const;

    /** Which subfolder a block type keeps its files in. */
    static juce::String subfolderFor (const juce::String& blockType);

private:
    juce::Result writeAtomically (const juce::String& text) const;
    void backUpCurrentFile() const;

    juce::File root;
};

} // namespace encore
