// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/ShowSession.h"
#include "session/PatchBuilder.h"

namespace encore
{

namespace
{
    constexpr int autosaveDelayMs = 2000;                     // after the last edit
    const auto backupInterval = juce::RelativeTime::minutes (5);
}

ShowSession::ShowSession (AudioEngine& e) : engine (e)
{
    startTimer (500);
}

ShowSession::~ShowSession()
{
    stopTimer();

    if (dirty && folder.isValid())
        writeToDisk (true);   // never lose edits on quit
}

//==============================================================================
juce::Result ShowSession::newShow (const juce::File& newFolder, const juce::String& name,
                                   const std::map<juce::String, juce::File>& starterFiles)
{
    if (dirty && folder.isValid())
        writeToDisk (true);

    auto created = makeDefaultShow (name);

    for (auto& block : created.patches[0].mainLane().blocks)
        if (const auto it = starterFiles.find (block.type); it != starterFiles.end() && it->second.existsAsFile())
            block.file = it->second.getFullPathName();   // copied into the show by create()

    if (const auto r = ShowFolder::create (newFolder, created); r.failed())
        return r;

    adoptShow (std::move (created), ShowFolder (newFolder));
    return juce::Result::ok();
}

juce::Result ShowSession::openShow (const juce::File& folderOrShowFile)
{
    ShowFolder opened;
    juce::String error;
    auto loaded = ShowFolder::open (folderOrShowFile, opened, error);

    if (! loaded)
        return juce::Result::fail (error);

    if (dirty && folder.isValid())
        writeToDisk (true);

    if (loaded->patches.empty())
        loaded->patches.push_back (makeDefaultPatch ("Clean"));

    adoptShow (std::move (*loaded), opened);
    return juce::Result::ok();
}

void ShowSession::adoptShow (ShowModel newShow, const ShowFolder& newFolder)
{
    show = std::move (newShow);
    folder = newFolder;
    dirty = false;
    lastSave = lastBackup = juce::Time::getCurrentTime();
    liveChain = nullptr;
    liveChainPatchId.clear();
    activePatchId = show.patches.empty() ? juce::String() : show.patches.front().id;

    rebuildActiveChain();
    sendChangeMessage();
}

juce::Result ShowSession::save()
{
    return writeToDisk (true);
}

juce::Result ShowSession::saveCopyTo (const juce::File& parentFolder, juce::File& copy)
{
    if (const auto r = writeToDisk (true); r.failed())
        return r;

    copy = ShowFolder::suggestFolder (parentFolder, show.name);

    if (! folder.getFolder().copyDirectoryTo (copy))
        return juce::Result::fail ("Couldn't copy the show to " + copy.getFullPathName());

    return juce::Result::ok();
}

juce::Result ShowSession::writeToDisk (bool keepBackup)
{
    if (! folder.isValid())
        return juce::Result::fail ("No show folder");

    captureActive();

    const auto now = juce::Time::getCurrentTime();
    const auto backupDue = keepBackup || now - lastBackup >= backupInterval;
    juce::StringArray assetProblems;
    const auto result = folder.save (show, backupDue, &assetProblems);

    if (backupDue)
        lastBackup = now;

    if (result.wasOk())
    {
        dirty = false;     // a file that couldn't be copied in doesn't make the save fail
        lastSave = now;
    }
    else
    {
        problems.add (result.getErrorMessage());
    }

    for (auto& p : assetProblems)
        problems.addIfNotAlreadyThere (p);

    sendChangeMessage();
    return result;
}

juce::StringArray ShowSession::takeProblems()
{
    auto taken = problems;
    problems.clear();
    return taken;
}

//==============================================================================
PatchModel* ShowSession::activePatch()
{
    return show.findPatch (activePatchId);
}

void ShowSession::captureActive()
{
    // Only if the playing chain really is the active patch (not one still
    // being replaced), so a stale chain never overwrites newer edits.
    if (liveChain == nullptr || liveChain != engine.getChain() || liveChainPatchId != activePatchId)
        return;

    if (auto* patch = activePatch())
        captureChain (*liveChain, *patch, folder);
}

void ShowSession::rebuildActiveChain()
{
    auto* patch = activePatch();

    if (patch == nullptr)
        return;

    const auto generation = ++buildGeneration;
    building = true;

    // The build works on copies, so later edits to the show can't race with it.
    const auto patchCopy = *patch;
    const auto folderCopy = folder;
    const auto spec = engine.getCurrentSpec();
    juce::WeakReference<ShowSession> weakThis (this);

    jobs->add ([patchCopy, folderCopy, spec, generation, weakThis]
    {
        juce::StringArray buildProblems;
        auto chain = buildChain (patchCopy, folderCopy, spec, buildProblems);
        auto holder = std::make_shared<std::unique_ptr<Chain>> (std::move (chain));

        juce::MessageManager::callAsync ([weakThis, holder, generation, buildProblems, patchId = patchCopy.id]
        {
            auto* self = weakThis.get();

            // A newer build was started meanwhile: this one is out of date.
            if (self == nullptr || generation != self->buildGeneration)
                return;

            self->liveChain = holder->get();
            self->liveChainPatchId = patchId;
            self->engine.setChain (std::move (*holder));
            self->building = false;
            self->problems.addArray (buildProblems);
            self->sendChangeMessage();
        });
    });
}

void ShowSession::selectPatch (const juce::String& patchId)
{
    if (patchId == activePatchId || show.findPatch (patchId) == nullptr)
        return;

    captureActive();   // keep the edits made to the patch we're leaving
    activePatchId = patchId;
    rebuildActiveChain();
    sendChangeMessage();
}

juce::String ShowSession::addPatch (const juce::String& name)
{
    captureActive();
    auto patch = makeDefaultPatch (name);

    // Start from the amp and cab currently in use: quicker than starting blank.
    if (auto* current = activePatch())
        for (auto& b : patch.mainLane().blocks)
            for (auto& existing : current->mainLane().blocks)
                if (existing.type == b.type && existing.file.isNotEmpty() && b.file.isEmpty())
                    b.file = existing.file;

    show.patches.push_back (patch);
    changed (true);
    selectPatch (patch.id);
    return patch.id;
}

juce::String ShowSession::duplicatePatch (const juce::String& patchId)
{
    captureActive();

    if (auto* original = show.findPatch (patchId))
    {
        auto copy = encore::duplicatePatch (*original, original->name + " copy");
        show.patches.push_back (copy);
        changed (true);
        selectPatch (copy.id);
        return copy.id;
    }

    return {};
}

void ShowSession::renamePatch (const juce::String& patchId, const juce::String& newName)
{
    if (auto* patch = show.findPatch (patchId); patch != nullptr && newName.trim().isNotEmpty())
    {
        patch->name = newName.trim();
        changed (true);
    }
}

bool ShowSession::deletePatch (const juce::String& patchId)
{
    if (show.patches.size() <= 1)
        return false;

    const auto it = std::find_if (show.patches.begin(), show.patches.end(),
                                  [&] (const PatchModel& p) { return p.id == patchId; });

    if (it == show.patches.end())
        return false;

    const auto index = (int) std::distance (show.patches.begin(), it);
    const auto wasActive = patchId == activePatchId;
    show.patches.erase (it);

    if (wasActive)
    {
        activePatchId.clear();
        selectPatch (show.patches[(size_t) juce::jmin (index, (int) show.patches.size() - 1)].id);
    }

    changed (true);
    return true;
}

//==============================================================================
void ShowSession::addBlock (const juce::String& typeId, int index)
{
    captureActive();

    if (auto* patch = activePatch())
    {
        auto& blocks = patch->mainLane().blocks;
        BlockModel block;
        block.id = newId();
        block.type = typeId;
        blocks.insert (blocks.begin() + juce::jlimit (0, (int) blocks.size(), index), block);
        changed (true);
        rebuildActiveChain();
    }
}

void ShowSession::removeBlock (int index)
{
    captureActive();

    if (auto* patch = activePatch(); patch != nullptr && juce::isPositiveAndBelow (index, (int) patch->mainLane().blocks.size()))
    {
        auto& blocks = patch->mainLane().blocks;
        blocks.erase (blocks.begin() + index);
        changed (true);
        rebuildActiveChain();
    }
}

void ShowSession::moveBlock (int from, int to)
{
    captureActive();

    if (auto* patch = activePatch())
    {
        auto& blocks = patch->mainLane().blocks;
        const auto size = (int) blocks.size();

        if (! juce::isPositiveAndBelow (from, size) || from == to)
            return;

        to = juce::jlimit (0, size - 1, to);
        auto block = blocks[(size_t) from];
        blocks.erase (blocks.begin() + from);
        blocks.insert (blocks.begin() + to, block);
        changed (true);
        rebuildActiveChain();
    }
}

void ShowSession::markEdited()
{
    changed (false);
}

void ShowSession::changed (bool showContentChanged)
{
    dirty = true;
    lastEdit = juce::Time::getCurrentTime();

    if (showContentChanged)
        sendChangeMessage();
}

void ShowSession::timerCallback()
{
    if (dirty && folder.isValid()
        && juce::Time::getCurrentTime() - lastEdit >= juce::RelativeTime::milliseconds (autosaveDelayMs))
        writeToDisk (false);
}

} // namespace encore
