// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/ShowFolder.h"
#include "model/ShowJson.h"

namespace encore
{

namespace
{
    bool isRelativePath (const juce::String& path)
    {
        return path.isNotEmpty() && ! juce::File::isAbsolutePath (path);
    }

    template <typename Fn>
    void forEachBlock (ShowModel& show, Fn&& fn)
    {
        auto visitPatch = [&] (PatchModel& patch)
        {
            for (auto& lane : patch.lanes)
                for (auto& b : lane.blocks)
                    fn (b);
        };

        for (auto& p : show.patches)
            visitPatch (p);

        std::function<void (RunItem&)> visitItem = [&] (RunItem& item)
        {
            if (item.uniquePatch)
                visitPatch (*item.uniquePatch);

            for (auto& child : item.children)
                visitItem (child);
        };

        visitItem (show.runningOrder);
    }
}

//==============================================================================
juce::File ShowFolder::suggestFolder (const juce::File& parent, const juce::String& showName)
{
    const auto base = juce::File::createLegalFileName (showName.trim().isEmpty() ? "Untitled show" : showName.trim());
    auto folder = parent.getChildFile (base + folderExtension);

    for (int n = 2; folder.exists(); ++n)
        folder = parent.getChildFile (base + " " + juce::String (n) + folderExtension);

    return folder;
}

juce::Result ShowFolder::create (const juce::File& folder, ShowModel& show)
{
    if (folder.exists() && ! folder.isDirectory())
        return juce::Result::fail (folder.getFullPathName() + " is a file, not a folder");

    for (auto sub : { "models", "irs", "backups" })
        if (const auto r = folder.getChildFile (sub).createDirectory(); r.failed())
            return juce::Result::fail ("Couldn't create the show folder: " + r.getErrorMessage());

    ShowFolder created (folder);
    return created.save (show, false);
}

std::optional<ShowModel> ShowFolder::open (const juce::File& folderOrShowFile, ShowFolder& opened, juce::String& error)
{
    const auto folder = folderOrShowFile.isDirectory() ? folderOrShowFile : folderOrShowFile.getParentDirectory();
    const auto showFile = folder.getChildFile (showFileName);

    if (! showFile.existsAsFile())
    {
        error = "No show found in " + folder.getFullPathName();
        return std::nullopt;
    }

    auto show = json::fromJson (showFile.loadFileAsString(), error);

    if (show)
        opened = ShowFolder (folder);

    return show;
}

//==============================================================================
juce::Result ShowFolder::save (ShowModel& show, bool keepBackupOfPrevious, juce::StringArray* assetProblems)
{
    jassert (isValid());

    // Make the folder self-contained before writing paths that point into it.
    const auto collected = collectAssets (show);

    if (collected.failed() && assetProblems != nullptr)
        assetProblems->addLines (collected.getErrorMessage());

    if (keepBackupOfPrevious)
        backUpCurrentFile();

    return writeAtomically (json::toJson (show));
}

juce::Result ShowFolder::writeAtomically (const juce::String& text) const
{
    if (const auto r = root.createDirectory(); r.failed())
        return r;

    const juce::TemporaryFile temp (getShowFile(), juce::TemporaryFile::useHiddenFile);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk())
            return juce::Result::fail ("Couldn't write to " + root.getFullPathName() + ": " + out.getStatus().getErrorMessage());

        out.writeText (text, false, false, "\n");
        out.flush();   // to disk before the swap

        if (out.getStatus().failed())
            return juce::Result::fail ("Saving failed: " + out.getStatus().getErrorMessage());
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Couldn't replace " + getShowFile().getFullPathName());

    return juce::Result::ok();
}

void ShowFolder::backUpCurrentFile() const
{
    const auto current = getShowFile();

    if (! current.existsAsFile())
        return;

    const auto backups = getBackupsFolder();
    backups.createDirectory();

    // Names sort in time order: date, time, milliseconds, then a padded counter.
    const auto now = juce::Time::getCurrentTime();
    const auto stamp = "show-" + now.formatted ("%Y-%m-%d_%H-%M-%S") + "-"
                     + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);
    auto target = backups.getChildFile (stamp + ".json");

    for (int n = 2; target.exists(); ++n)
        target = backups.getChildFile (stamp + "_" + juce::String (n).paddedLeft ('0', 3) + ".json");

    current.copyFileTo (target);

    // Keep the newest few; timestamps sort chronologically by name.
    auto all = listBackups();

    for (int i = maxBackups; i < all.size(); ++i)
        all[i].deleteFile();
}

juce::Array<juce::File> ShowFolder::listBackups() const
{
    auto files = getBackupsFolder().findChildFiles (juce::File::findFiles, false, "show-*.json");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName() > b.getFileName();   // newest first
    });
    return files;
}

//==============================================================================
juce::String ShowFolder::subfolderFor (const juce::String& blockType)
{
    if (blockType == "nam") return "models";
    if (blockType == "cab") return "irs";
    return "files";
}

juce::File ShowFolder::resolve (const juce::String& storedPath) const
{
    if (storedPath.isEmpty())
        return {};

    if (isRelativePath (storedPath))
        return root.getChildFile (storedPath);

    return juce::File (storedPath);
}

juce::String ShowFolder::importAsset (const juce::File& source, const juce::String& subfolder, juce::String& error) const
{
    error.clear();

    if (source.isAChildOf (root))
        return source.getRelativePathFrom (root).replaceCharacter ('\\', '/');

    if (! source.existsAsFile())
    {
        error = "Missing file: " + source.getFullPathName();
        return {};
    }

    const auto folder = root.getChildFile (subfolder);
    folder.createDirectory();

    auto target = folder.getChildFile (source.getFileName());

    // Same name: reuse it if it's the same file, else pick a free numbered name.
    for (int n = 2; target.existsAsFile() && ! target.hasIdenticalContentTo (source); ++n)
        target = folder.getChildFile (source.getFileNameWithoutExtension() + " (" + juce::String (n) + ")"
                                      + source.getFileExtension());

    if (! target.existsAsFile() && ! source.copyFileTo (target))
    {
        error = "Couldn't copy " + source.getFileName() + " into the show";
        return {};
    }

    return subfolder + "/" + target.getFileName();
}

juce::Result ShowFolder::collectAssets (ShowModel& show) const
{
    juce::StringArray problems;

    forEachBlock (show, [&] (BlockModel& block)
    {
        if (block.file.isEmpty() || isRelativePath (block.file))
            return;

        juce::String error;
        const auto relative = importAsset (juce::File (block.file), subfolderFor (block.type), error);

        if (relative.isNotEmpty())
            block.file = relative;
        else
            problems.addIfNotAlreadyThere (error);
    });

    return problems.isEmpty() ? juce::Result::ok() : juce::Result::fail (problems.joinIntoString ("\n"));
}

} // namespace encore
