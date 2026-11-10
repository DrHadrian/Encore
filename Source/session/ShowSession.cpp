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
    const juce::String cueKeyPrefix ("cue:");

    Chain::TailMode toTailMode (Spillover s)
    {
        return s == Spillover::fullChain ? Chain::TailMode::fullChain : Chain::TailMode::tailsOnly;
    }

    /** Where each playable cue lives in the tree: its parent's children and its position. */
    struct CueRef
    {
        std::vector<RunItem>* siblings;
        int position;
    };

    void collectCues (RunItem& section, std::vector<CueRef>& out)
    {
        for (int i = 0; i < (int) section.children.size(); ++i)
        {
            auto& item = section.children[(size_t) i];

            if (item.cut)
                continue;

            if (item.kind == RunItem::Kind::cue)
                out.push_back ({ &section.children, i });
            else
                collectCues (item, out);
        }
    }

    RunItem* findCueById (RunItem& item, const juce::String& id)
    {
        if (item.kind == RunItem::Kind::cue && item.id == id)
            return &item;

        for (auto& child : item.children)
            if (auto* found = findCueById (child, id))
                return found;

        return nullptr;
    }
}

ShowSession::ShowSession (AudioEngine& e) : engine (e), player (e.getPlayer())
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
    // Let go of the old show's patches (they ring out and are freed when silent).
    for (auto& [key, chain] : chains)
        player.release (chain);

    chains.clear();
    buildGenerations.clear();
    player.publish ({});

    show = std::move (newShow);
    folder = newFolder;
    dirty = false;
    lastSave = lastBackup = juce::Time::getCurrentTime();
    ++showGeneration;
    currentCue = -1;
    lastSeenActive = nullptr;

    const auto cues = getCues();
    activePatchId = ! cues.empty() ? keyForCue (*cues.front())
                                   : (show.patches.empty() ? juce::String() : show.patches.front().id);

    buildAll();
    sendChangeMessage();
}

//==============================================================================
juce::String ShowSession::keyForCue (const RunItem& cue)
{
    return cue.uniquePatch ? cueKeyPrefix + cue.id : cue.patchId;
}

PatchModel* ShowSession::findPatchForKey (const juce::String& key)
{
    if (key.startsWith (cueKeyPrefix))
    {
        auto* cue = findCueById (show.runningOrder, key.substring (cueKeyPrefix.length()));
        return cue != nullptr && cue->uniquePatch ? &*cue->uniquePatch : nullptr;
    }

    return show.findPatch (key);
}

void ShowSession::buildAll()
{
    // Everything the show can play: library patches and cues' own patches.
    std::vector<std::pair<juce::String, PatchModel>> toBuild;

    for (auto& p : show.patches)
        toBuild.emplace_back (p.id, p);

    for (auto* cue : getCues())
        if (cue->uniquePatch)
            toBuild.emplace_back (keyForCue (*cue), *cue->uniquePatch);

    // The patch that plays first is built first, so the show makes sound sooner.
    std::stable_partition (toBuild.begin(), toBuild.end(),
                           [this] (const auto& entry) { return entry.first == activePatchId; });

    if (toBuild.empty())
        return;

    const auto generation = showGeneration;
    const auto folderCopy = folder;
    const auto spec = engine.getCurrentSpec();
    const auto startAtFirstCue = ! getCues().empty();
    juce::WeakReference<ShowSession> weakThis (this);
    ++pendingBuilds;

    jobs->add ([toBuild, generation, folderCopy, spec, startAtFirstCue, weakThis]
    {
        for (size_t i = 0; i < toBuild.size(); ++i)
        {
            const auto& [key, patch] = toBuild[i];
            juce::StringArray buildProblems;
            auto chain = buildChain (patch, folderCopy, spec, buildProblems);
            chain->label = patch.name;
            chain->setTailMode (toTailMode (patch.spillover));
            chain->setSwitchFades (patch.fadeInMs * 0.001, patch.ringOutMs * 0.001);
            chain->saveSnapshot();

            auto holder = std::make_shared<std::unique_ptr<Chain>> (std::move (chain));
            const auto isFirst = i == 0, isLast = i + 1 == toBuild.size();

            juce::MessageManager::callAsync ([weakThis, holder, key = key, buildProblems, generation,
                                              isFirst, isLast, startAtFirstCue]
            {
                auto* self = weakThis.get();

                if (self == nullptr || generation != self->showGeneration)
                    return;   // another show was opened meanwhile

                self->problems.addArray (buildProblems);
                self->installChain (key, std::move (*holder));
                self->publishRunningOrder();

                if (isFirst)
                {
                    if (startAtFirstCue)
                        self->player.goToCue (0);
                    else
                        self->player.play (self->chains[key]);
                }

                if (isLast)
                    --self->pendingBuilds;

                self->sendChangeMessage();
            });
        }
    });
}

void ShowSession::rebuildPatch (const juce::String& key)
{
    auto* patch = findPatchForKey (key);

    if (patch == nullptr)
        return;

    const auto generation = ++buildGenerations[key];
    const auto showGen = showGeneration;
    const auto patchCopy = *patch;
    const auto folderCopy = folder;
    const auto spec = engine.getCurrentSpec();
    juce::WeakReference<ShowSession> weakThis (this);
    ++pendingBuilds;

    jobs->add ([key, patchCopy, folderCopy, spec, generation, showGen, weakThis]
    {
        juce::StringArray buildProblems;
        auto chain = buildChain (patchCopy, folderCopy, spec, buildProblems);
        chain->label = patchCopy.name;
        chain->setTailMode (toTailMode (patchCopy.spillover));
        chain->setSwitchFades (patchCopy.fadeInMs * 0.001, patchCopy.ringOutMs * 0.001);
        chain->saveSnapshot();
        auto holder = std::make_shared<std::unique_ptr<Chain>> (std::move (chain));

        juce::MessageManager::callAsync ([weakThis, holder, key, buildProblems, generation, showGen]
        {
            auto* self = weakThis.get();

            if (self == nullptr)
                return;

            --self->pendingBuilds;

            // Out of date: another show, or a newer rebuild of this patch.
            if (showGen != self->showGeneration || generation != self->buildGenerations[key])
                return;

            const auto wasPlaying = self->chains.count (key) > 0
                                    && self->chains[key] == self->player.getActiveChain();
            const auto wantedNow = key == self->activePatchId;

            self->problems.addArray (buildProblems);
            self->installChain (key, std::move (*holder));
            self->publishRunningOrder();

            // The edited (or new) patch is the one being played: switch to the new
            // version (gapless, like any patch change).
            if (wasPlaying || wantedNow)
                self->player.play (self->chains[key]);

            self->sendChangeMessage();
        });
    });
}

void ShowSession::installChain (const juce::String& key, std::unique_ptr<Chain> chain)
{
    auto* raw = player.adopt (std::move (chain));
    auto it = chains.find (key);

    if (it != chains.end())
        player.release (it->second);   // freed once nothing can reach it

    chains[key] = raw;
}

void ShowSession::publishRunningOrder()
{
    std::vector<PatchPlayer::Cue> cues;

    for (auto* cue : getCues())
    {
        const auto it = chains.find (keyForCue (*cue));
        cues.push_back ({ it != chains.end() ? it->second : nullptr,
                          cue->samePatch == SamePatchBehaviour::reset });
    }

    player.publish (std::move (cues));
}

void ShowSession::syncWithAudio()
{
    auto* active = player.getActiveChain();
    const auto cue = player.getCurrentCue();

    if (active == lastSeenActive && cue == currentCue)
        return;

    lastSeenActive = active;
    currentCue = cue;

    for (auto& [key, chain] : chains)
        if (chain == active)
            activePatchId = key;

    sendChangeMessage();
}

void ShowSession::captureAll()
{
    for (auto& [key, chain] : chains)
    {
        if (auto* patch = findPatchForKey (key))
        {
            captureChain (*chain, *patch, folder);
            chain->saveSnapshot();   // "reset to saved settings" now means these
        }
    }
}

//==============================================================================
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

    captureAll();

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
void ShowSession::selectPatch (const juce::String& patchId)
{
    if (const auto it = chains.find (patchId); it != chains.end())
    {
        player.play (it->second);
        activePatchId = patchId;   // the audio thread confirms it on its next buffer
        sendChangeMessage();
    }
}

juce::String ShowSession::addPatch (const juce::String& name)
{
    captureAll();
    auto patch = makeDefaultPatch (name);

    // Start from the amp and cab currently in use: quicker than starting blank.
    if (auto* current = findPatchForKey (activePatchId))
        for (auto& b : patch.mainLane().blocks)
            for (auto& existing : current->mainLane().blocks)
                if (existing.type == b.type && existing.file.isNotEmpty() && b.file.isEmpty())
                    b.file = existing.file;

    show.patches.push_back (patch);
    activePatchId = patch.id;   // plays as soon as it's built
    changed (true);
    rebuildPatch (patch.id);
    return patch.id;
}

juce::String ShowSession::duplicatePatch (const juce::String& patchId)
{
    captureAll();

    if (auto* original = findPatchForKey (patchId))
    {
        auto copy = encore::duplicatePatch (*original, original->name + " copy");
        show.patches.push_back (copy);
        activePatchId = copy.id;
        changed (true);
        rebuildPatch (copy.id);
        return copy.id;
    }

    return {};
}

void ShowSession::renamePatch (const juce::String& patchId, const juce::String& newName)
{
    if (auto* patch = findPatchForKey (patchId); patch != nullptr && newName.trim().isNotEmpty())
    {
        patch->name = newName.trim();

        if (const auto it = chains.find (patchId); it != chains.end())
            it->second->label = patch->name;

        changed (true);
    }
}

int ShowSession::countCuesUsing (const juce::String& patchId) const
{
    int count = 0;

    for (auto* cue : getCues())
        count += (! cue->uniquePatch && cue->patchId == patchId) ? 1 : 0;

    return count;
}

bool ShowSession::deletePatch (const juce::String& patchId)
{
    if (show.patches.size() <= 1)
        return false;

    const auto it = std::find_if (show.patches.begin(), show.patches.end(),
                                  [&] (const PatchModel& p) { return p.id == patchId; });

    if (it == show.patches.end())
        return false;

    captureAll();
    const auto index = (int) std::distance (show.patches.begin(), it);
    show.patches.erase (it);

    const auto wasActive = patchId == activePatchId;

    if (const auto chainIt = chains.find (patchId); chainIt != chains.end())
    {
        player.release (chainIt->second);
        chains.erase (chainIt);
    }

    publishRunningOrder();   // cues using it now keep their place but change nothing

    if (wasActive)
        selectPatch (show.patches[(size_t) juce::jmin (index, (int) show.patches.size() - 1)].id);

    changed (true);
    return true;
}

void ShowSession::setSpillover (const juce::String& patchId, Spillover mode)
{
    if (auto* patch = findPatchForKey (patchId))
    {
        patch->spillover = mode;

        if (const auto it = chains.find (patchId); it != chains.end())
            it->second->setTailMode (toTailMode (mode));

        changed (true);
    }
}

void ShowSession::setSwitchFades (const juce::String& patchId, float fadeInMs, float ringOutMs)
{
    if (auto* patch = findPatchForKey (patchId))
    {
        patch->fadeInMs = fadeInMs;
        patch->ringOutMs = ringOutMs;

        if (const auto it = chains.find (patchId); it != chains.end())
            it->second->setSwitchFades (fadeInMs * 0.001, ringOutMs * 0.001);

        changed (true);
    }
}

//==============================================================================
void ShowSession::addBlock (const juce::String& typeId, int index)
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId))
    {
        auto& blocks = patch->mainLane().blocks;
        BlockModel block;
        block.id = newId();
        block.type = typeId;
        blocks.insert (blocks.begin() + juce::jlimit (0, (int) blocks.size(), index), block);
        changed (true);
        rebuildPatch (activePatchId);
    }
}

void ShowSession::removeBlock (int index)
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId);
        patch != nullptr && juce::isPositiveAndBelow (index, (int) patch->mainLane().blocks.size()))
    {
        auto& blocks = patch->mainLane().blocks;
        blocks.erase (blocks.begin() + index);
        changed (true);
        rebuildPatch (activePatchId);
    }
}

void ShowSession::moveBlock (int from, int to)
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId))
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
        rebuildPatch (activePatchId);
    }
}

void ShowSession::markEdited()
{
    changed (false);
}

//==============================================================================
std::vector<const RunItem*> ShowSession::getCues() const
{
    std::vector<CueRef> refs;
    collectCues (const_cast<RunItem&> (show.runningOrder), refs);

    std::vector<const RunItem*> cues;

    for (auto& r : refs)
        cues.push_back (&(*r.siblings)[(size_t) r.position]);

    return cues;
}

bool ShowSession::locateCue (int index, std::vector<RunItem>*& siblings, int& position)
{
    std::vector<CueRef> refs;
    collectCues (show.runningOrder, refs);

    if (! juce::isPositiveAndBelow (index, (int) refs.size()))
        return false;

    siblings = refs[(size_t) index].siblings;
    position = refs[(size_t) index].position;
    return true;
}

RunItem* ShowSession::cueAt (int index)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;
    return locateCue (index, siblings, position) ? &(*siblings)[(size_t) position] : nullptr;
}

void ShowSession::next()             { player.next(); }
void ShowSession::previous()         { player.previous(); }
void ShowSession::goToCue (int index) { player.goToCue (index); }

juce::String ShowSession::addCue (const juce::String& patchId, int afterIndex)
{
    const auto* patch = show.findPatch (patchId);
    auto cue = RunItem::makeCue ({}, patch != nullptr ? patch->name : juce::String ("Cue"), patchId);
    const auto currentId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();

    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (locateCue (afterIndex, siblings, position))
        siblings->insert (siblings->begin() + position + 1, cue);
    else
        show.runningOrder.children.push_back (cue);

    publishRunningOrder();

    // Keep "next" continuing from the cue that is playing, wherever it moved to.
    const auto cues = getCues();

    for (int i = 0; i < (int) cues.size(); ++i)
        if (cues[(size_t) i]->id == currentId)
            player.setCuePosition (i);

    changed (true);
    return cue.id;
}

void ShowSession::removeCue (int index)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (! locateCue (index, siblings, position))
        return;

    siblings->erase (siblings->begin() + position);
    publishRunningOrder();

    if (index < currentCue)
        player.setCuePosition (currentCue - 1);
    else if (index == currentCue)
        player.setCuePosition (currentCue - 1);   // "next" plays the cue that followed

    changed (true);
}

void ShowSession::moveCue (int index, int delta)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (! locateCue (index, siblings, position))
        return;

    const auto target = position + delta;

    if (! juce::isPositiveAndBelow (target, (int) siblings->size()))
        return;

    const auto currentId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();
    std::swap ((*siblings)[(size_t) position], (*siblings)[(size_t) target]);
    publishRunningOrder();

    const auto cues = getCues();

    for (int i = 0; i < (int) cues.size(); ++i)
        if (cues[(size_t) i]->id == currentId)
            player.setCuePosition (i);

    changed (true);
}

void ShowSession::renameCue (int index, const juce::String& name)
{
    if (auto* cue = cueAt (index))
    {
        cue->name = name.trim();
        changed (true);
    }
}

void ShowSession::setCuePatch (int index, const juce::String& patchId)
{
    if (auto* cue = cueAt (index); cue != nullptr && show.findPatch (patchId) != nullptr)
    {
        cue->patchId = patchId;
        cue->uniquePatch.reset();
        publishRunningOrder();
        changed (true);
    }
}

void ShowSession::setCueSamePatch (int index, SamePatchBehaviour behaviour)
{
    if (auto* cue = cueAt (index))
    {
        cue->samePatch = behaviour;
        publishRunningOrder();
        changed (true);
    }
}

//==============================================================================
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
