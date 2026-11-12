// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/ShowSession.h"
#include "session/PatchBuilder.h"
#include "session/PluginCatalog.h"
#include "engine/blocks/PluginBlock.h"

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

    /** The list holding an item (sections included), and where in it. */
    bool locateItem (RunItem& parent, const juce::String& id, std::vector<RunItem>*& siblings, int& position)
    {
        for (int i = 0; i < (int) parent.children.size(); ++i)
        {
            if (parent.children[(size_t) i].id == id)
            {
                siblings = &parent.children;
                position = i;
                return true;
            }

            if (locateItem (parent.children[(size_t) i], id, siblings, position))
                return true;
        }

        return false;
    }

    /** The section whose children are `list` (or null for the root's). */
    RunItem* ownerOf (RunItem& item, const std::vector<RunItem>* list)
    {
        if (&item.children == list)
            return &item;

        for (auto& child : item.children)
            if (auto* found = ownerOf (child, list))
                return found;

        return nullptr;
    }

    void collectRows (const RunItem& section, int depth, int& cueIndex, std::vector<ShowSession::Row>& out)
    {
        for (auto& item : section.children)
        {
            const auto playable = item.kind == RunItem::Kind::cue && ! item.cut;
            out.push_back ({ &item, depth, playable ? cueIndex : -1 });

            if (playable)
                ++cueIndex;

            if (item.kind == RunItem::Kind::section)
                collectRows (item, depth + 1, cueIndex, out);
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
    buildsWanted->fetch_add (1);   // plugin creation still waiting gives up

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
    renumberCues();   // numbers always follow the order (older shows may differ)
    folder = newFolder;
    dirty = false;
    lastSave = lastBackup = juce::Time::getCurrentTime();
    ++showGeneration;
    buildsWanted->fetch_add (1);
    reportedFaults.clear();
    currentCue = -1;
    lastSeenActive = nullptr;

    const auto cues = getCues();
    activePatchId = ! cues.empty() ? keyForCue (*cues.front())
                                   : (show.patches.empty() ? juce::String() : show.patches.front().id);

    applyStageSettings();
    buildAll();
    sendChangeMessage();
}

//==============================================================================
void ShowSession::applyStageSettings()
{
    engine.setTunerMutes (show.stage.tunerMutes);
    engine.getTuner().setReferencePitch (show.stage.referencePitch);
    engine.setPanicMode (show.stage.panic == PanicBehaviour::latch ? AudioEngine::PanicMode::latch
                                                                   : AudioEngine::PanicMode::recover);
}

void ShowSession::ensureLayouts()
{
    // Shows start with the built-in layout; it becomes the show's own once edited.
    if (show.stage.layouts.empty())
    {
        show.stage.layouts.push_back (makeDefaultStageLayout());
        show.stage.defaultLayoutId = show.stage.layouts.front().id;
    }
}

const StageLayout& ShowSession::getLayoutForCue (int index) const
{
    static const auto builtIn = makeDefaultStageLayout();
    const auto cues = getCues();
    const auto own = juce::isPositiveAndBelow (index, (int) cues.size()) ? cues[(size_t) index]->layoutId : juce::String();

    if (const auto* layout = show.stage.findLayout (own))
        return *layout;

    return builtIn;
}

juce::String ShowSession::addLayout (const juce::String& name, const juce::String& copyFromId)
{
    ensureLayouts();

    StageLayout layout;

    if (const auto* source = show.stage.findLayout (copyFromId))
        layout = *source;
    else
        layout = makeDefaultStageLayout();

    layout.id = juce::Uuid().toString();
    layout.name = name.isNotEmpty() ? name : juce::String ("Layout");
    show.stage.layouts.push_back (layout);
    changed (true);
    return layout.id;
}

void ShowSession::renameLayout (const juce::String& id, const juce::String& name)
{
    ensureLayouts();

    for (auto& layout : show.stage.layouts)
        if (layout.id == id && name.isNotEmpty())
            layout.name = name;

    changed (true);
}

bool ShowSession::deleteLayout (const juce::String& id)
{
    ensureLayouts();
    auto& layouts = show.stage.layouts;

    if (layouts.size() <= 1)
        return false;

    layouts.erase (std::remove_if (layouts.begin(), layouts.end(), [&] (const StageLayout& l) { return l.id == id; }),
                   layouts.end());

    if (show.stage.defaultLayoutId == id)
        show.stage.defaultLayoutId = layouts.front().id;

    // Cues that used it fall back to the default (their id no longer matches).
    changed (true);
    return true;
}

void ShowSession::setDefaultLayout (const juce::String& id)
{
    ensureLayouts();
    show.stage.defaultLayoutId = id;
    changed (true);
}

void ShowSession::setLayoutWidgets (const juce::String& id, std::vector<StageWidget> widgets)
{
    ensureLayouts();

    for (auto& w : widgets)
        w = clampToGrid (w);

    for (auto& layout : show.stage.layouts)
    {
        if (layout.id == id && layout.widgets != widgets)
        {
            layout.widgets = std::move (widgets);
            changed (true);
            return;
        }
    }
}

void ShowSession::setCueLayout (int index, const juce::String& layoutId)
{
    if (auto* cue = cueAt (index))
    {
        cue->layoutId = layoutId;
        changed (true);
    }
}

void ShowSession::setTunerMutes (bool mutes)
{
    show.stage.tunerMutes = mutes;
    applyStageSettings();
    changed (true);
}

void ShowSession::setReferencePitch (float hz)
{
    show.stage.referencePitch = juce::jlimit (415.0f, 466.0f, hz);
    applyStageSettings();
    changed (true);
}

void ShowSession::setPanicBehaviour (PanicBehaviour behaviour)
{
    show.stage.panic = behaviour;
    applyStageSettings();
    changed (true);
}

//==============================================================================
juce::String ShowSession::keyForCue (const RunItem& cue)
{
    return cue.uniquePatch ? cueKeyPrefix + cue.id : cue.patchId;
}

std::vector<ShowSession::PatchEntry> ShowSession::getAllPatches() const
{
    std::vector<PatchEntry> list;

    for (const auto& p : show.patches)
        list.push_back ({ p.id, p.name, &p });

    const auto item = getWords().Item();

    for (const auto* cue : getCues())
        if (cue->uniquePatch)
            list.push_back ({ keyForCue (*cue),
                              item + " " + cue->number + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + cue->name + " (own patch)",
                              &*cue->uniquePatch });

    return list;
}

int ShowSession::getPatchOutputChannels (const juce::String& patchKey) const
{
    const auto it = chains.find (patchKey);
    return it != chains.end() && it->second != nullptr ? it->second->getOutputChannels() : 0;
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

const PatchModel* ShowSession::findPatchForKey (const juce::String& key) const
{
    return const_cast<ShowSession*> (this)->findPatchForKey (key);
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
    const auto plugins = makePluginContext();
    juce::WeakReference<ShowSession> weakThis (this);
    ++pendingBuilds;

    jobs->add ([toBuild, generation, folderCopy, spec, startAtFirstCue, plugins, weakThis]
    {
        for (size_t i = 0; i < toBuild.size(); ++i)
        {
            const auto& [key, patch] = toBuild[i];
            juce::StringArray buildProblems;
            auto chain = buildChain (patch, folderCopy, spec, buildProblems, plugins);
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
    const auto plugins = makePluginContext();
    juce::WeakReference<ShowSession> weakThis (this);
    ++pendingBuilds;

    jobs->add ([key, patchCopy, folderCopy, spec, generation, showGen, plugins, weakThis]
    {
        juce::StringArray buildProblems;
        auto chain = buildChain (patchCopy, folderCopy, spec, buildProblems, plugins);
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

void ShowSession::applyRouting (Chain& chain, const PatchModel& patch) const
{
    engine.applyPatchRouting (chain, patch.inputName(), [&patch] (const juce::String& output)
    {
        const auto send = patch.sendFor (output);
        return send.on ? routing::gainFor (send.levelDb) : 0.0f;
    });
}

void ShowSession::refreshRouting()
{
    for (auto& [key, chain] : chains)
        if (const auto* patch = findPatchForKey (key))
            applyRouting (*chain, *patch);
}

juce::StringArray ShowSession::getUnalignedPatches() const
{
    juce::StringArray names;

    for (const auto& [key, chain] : chains)
        if (chain->isAlignmentLimited())
            if (const auto* patch = findPatchForKey (key))
                names.addIfNotAlreadyThere (patch->name);

    names.sort (true);
    return names;
}

void ShowSession::installChain (const juce::String& key, std::unique_ptr<Chain> chain)
{
    // Its input and sends before it can be heard.
    if (const auto* patch = findPatchForKey (key))
        applyRouting (*chain, *patch);

    auto* raw = player.adopt (std::move (chain));
    auto it = chains.find (key);

    if (it != chains.end())
        player.release (it->second);   // freed once nothing can reach it

    chains[key] = raw;
}

void ShowSession::publishRunningOrder()
{
    std::vector<PatchPlayer::Cue> cues;
    std::vector<std::vector<MidiOutMessage>> midiOut;

    for (auto* cue : getCues())
    {
        const auto it = chains.find (keyForCue (*cue));
        cues.push_back ({ it != chains.end() ? it->second : nullptr,
                          cue->samePatch == SamePatchBehaviour::reset,
                          cue->programChange });
        midiOut.push_back (cue->midiOut);
    }

    player.publish (std::move (cues));
    engine.getMidiRouter().setCueMidiOut (std::move (midiOut));
}

void ShowSession::syncWithAudio()
{
    checkPluginFaults();

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
namespace
{
    /** The lane a new block goes into (0 = main, 1 = path B, which needs a split). */
    std::vector<BlockModel>* laneFor (PatchModel& patch, int lane)
    {
        if (lane == 1)
            return patch.pathB() != nullptr ? &patch.pathB()->blocks : nullptr;

        return &patch.mainLane().blocks;
    }

    bool isSplitOrMerge (const BlockModel& b)
    {
        return b.type == splitBlockType || b.type == mergeBlockType;
    }
}

void ShowSession::addBlock (const juce::String& typeId, int index, int lane)
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId))
    {
        if (typeId == splitBlockType || typeId == mergeBlockType)
            return;   // a split comes whole: addSplit()

        if (auto* blocks = laneFor (*patch, lane))
        {
            BlockModel block;
            block.id = newId();
            block.type = typeId;
            blocks->insert (blocks->begin() + juce::jlimit (0, (int) blocks->size(), index), block);
            changed (true);
            rebuildPatch (activePatchId);
        }
    }
}

void ShowSession::addPluginBlock (const PluginRef& plugin, int index, int lane)
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId))
    {
        if (auto* blocks = laneFor (*patch, lane))
        {
            BlockModel block;
            block.id = newId();
            block.type = pluginBlockType;
            block.plugin = plugin;
            blocks->insert (blocks->begin() + juce::jlimit (0, (int) blocks->size(), index), block);
            changed (true);
            rebuildPatch (activePatchId);
        }
    }
}

void ShowSession::removeBlock (int index)
{
    captureAll();
    auto* patch = findPatchForKey (activePatchId);
    int lane = 0, at = 0;

    if (patch == nullptr || ! patch->locate (index, lane, at))
        return;

    auto& blocks = patch->lanes[(size_t) lane].blocks;

    // The split or the merge: the split goes (path A stays, path B with it).
    if (isSplitOrMerge (blocks[(size_t) at]))
        encore::removeSplit (*patch);
    else
        blocks.erase (blocks.begin() + at);

    changed (true);
    rebuildPatch (activePatchId);
}

void ShowSession::moveBlock (int from, int to)
{
    captureAll();
    auto* patch = findPatchForKey (activePatchId);
    int lane = 0, at = 0;

    if (patch == nullptr || from == to || ! patch->locate (from, lane, at))
        return;

    // Within its own lane (path B's blocks stay in path B); the split and the
    // merge stay where they are, and nothing jumps over them.
    auto& blocks = patch->lanes[(size_t) lane].blocks;
    const auto laneStart = from - at;
    const auto target = juce::jlimit (0, (int) blocks.size() - 1, to - laneStart);

    if (target == at || isSplitOrMerge (blocks[(size_t) at]))
        return;

    for (int i = juce::jmin (at, target); i <= juce::jmax (at, target); ++i)
        if (i != at && isSplitOrMerge (blocks[(size_t) i]))
            return;

    auto block = blocks[(size_t) at];
    blocks.erase (blocks.begin() + at);
    blocks.insert (blocks.begin() + target, block);
    changed (true);
    rebuildPatch (activePatchId);
}

void ShowSession::moveBlockToPath (int index, bool toPathB)
{
    captureAll();
    auto* patch = findPatchForKey (activePatchId);
    int lane = 0, at = 0;

    if (patch == nullptr || ! patch->hasSplit() || ! patch->locate (index, lane, at))
        return;

    const auto split = patch->splitIndex(), merge = patch->mergeIndex();
    auto& main = patch->mainLane().blocks;
    auto& pathB = patch->pathB()->blocks;

    if (toPathB && lane == 0 && at > split && at < merge)
    {
        // From path A to the end of path B.
        pathB.push_back (main[(size_t) at]);
        main.erase (main.begin() + at);
    }
    else if (! toPathB && lane == 1)
    {
        // From path B to the end of path A (just before the merge).
        main.insert (main.begin() + merge, pathB[(size_t) at]);
        pathB.erase (pathB.begin() + at);
    }
    else
    {
        return;
    }

    changed (true);
    rebuildPatch (activePatchId);
}

bool ShowSession::addSplit (int index)
{
    captureAll();
    auto* patch = findPatchForKey (activePatchId);

    if (patch == nullptr || ! insertSplit (*patch, index))
        return false;

    changed (true);
    rebuildPatch (activePatchId);
    return true;
}

void ShowSession::removeSplit()
{
    captureAll();

    if (auto* patch = findPatchForKey (activePatchId); patch != nullptr && patch->hasSplit())
    {
        encore::removeSplit (*patch);
        changed (true);
        rebuildPatch (activePatchId);
    }
}

void ShowSession::setPatchInput (const juce::String& patchKey, const juce::String& inputName)
{
    if (auto* patch = findPatchForKey (patchKey))
    {
        patch->mainLane().input = routing::cleanName (inputName);

        // Live at once: the input is an atomic on the chain.
        if (const auto it = chains.find (patchKey); it != chains.end())
            applyRouting (*it->second, *patch);

        changed (true);
    }
}

void ShowSession::setPatchSend (const juce::String& patchKey, const juce::String& outputName, float levelDb, bool on)
{
    if (auto* patch = findPatchForKey (patchKey); patch != nullptr && outputName.trim().isNotEmpty())
    {
        patch->setSend (outputName.trim(), levelDb, on);

        // Live at once (ramped): sends are atomics on the chain.
        if (const auto it = chains.find (patchKey); it != chains.end())
            applyRouting (*it->second, *patch);

        changed (true);
    }
}

void ShowSession::markEdited()
{
    changed (false);
}

void ShowSession::setBlockStomp (int blockIndex, int stomp)
{
    auto* patch = findPatchForKey (activePatchId);
    auto* block = patch != nullptr ? patch->blockAt (blockIndex) : nullptr;

    if (block == nullptr || block->type == mergeBlockType)
        return;

    stomp = juce::jlimit (0, maxStomps, stomp);
    block->stomp = stomp;

    // Live at once, no rebuild: the stomp number is an atomic on the block.
    if (const auto it = chains.find (activePatchId); it != chains.end()
        && juce::isPositiveAndBelow (blockIndex, (int) it->second->getBlocks().size()))
        it->second->getBlocks()[(size_t) blockIndex]->setStomp (stomp);

    changed (true);
}

int ShowSession::getBlockStomp (int blockIndex) const
{
    const auto* patch = findPatchForKey (activePatchId);
    const auto* block = patch != nullptr ? patch->blockAt (blockIndex) : nullptr;
    return block != nullptr ? block->stomp : 0;
}

void ShowSession::setExpression (int blockIndex, const juce::String& paramId, int pedal)
{
    captureAll();
    auto* patch = findPatchForKey (activePatchId);
    auto* block = patch != nullptr ? patch->blockAt (blockIndex) : nullptr;

    if (block == nullptr)
        return;

    if (pedal <= 0)
    {
        block->expression.erase (paramId);
    }
    else
    {
        // Full range of the knob: heel = minimum, toe = maximum.
        ExpressionAssignment assignment;
        assignment.pedal = juce::jlimit (1, maxExpressionPedals, pedal);

        if (const auto it = chains.find (activePatchId); it != chains.end()
            && juce::isPositiveAndBelow (blockIndex, (int) it->second->getBlocks().size()))
            if (auto* p = it->second->getBlocks()[(size_t) blockIndex]->findParameter (paramId))
            {
                assignment.min = p->getSpec().range.start;
                assignment.max = p->getSpec().range.end;
            }

        block->expression[paramId] = assignment;
    }

    changed (true);
    rebuildPatch (activePatchId);
}

int ShowSession::getExpressionPedal (int blockIndex, const juce::String& paramId) const
{
    const auto assignment = getExpressionAssignment (blockIndex, paramId);
    return assignment.has_value() ? assignment->pedal : 0;
}

std::optional<ExpressionAssignment> ShowSession::getExpressionAssignment (int blockIndex, const juce::String& paramId) const
{
    const auto* patch = findPatchForKey (activePatchId);
    const auto* block = patch != nullptr ? patch->blockAt (blockIndex) : nullptr;

    if (block == nullptr)
        return std::nullopt;

    const auto it = block->expression.find (paramId);

    if (it == block->expression.end())
        return std::nullopt;

    return it->second;
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

//==============================================================================
std::vector<ShowSession::Row> ShowSession::getRunningOrderRows() const
{
    std::vector<Row> rows;
    int cueIndex = 0;
    collectRows (show.runningOrder, 0, cueIndex, rows);
    return rows;
}

RunItem* ShowSession::findItem (const juce::String& id)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;
    return locateItem (show.runningOrder, id, siblings, position) ? &(*siblings)[(size_t) position] : nullptr;
}

int ShowSession::getCueIndexOf (const juce::String& id) const
{
    const auto cues = getCues();

    for (int i = 0; i < (int) cues.size(); ++i)
        if (cues[(size_t) i]->id == id)
            return i;

    return -1;
}

int ShowSession::findCueByNumber (const juce::String& number) const
{
    const auto wanted = number.trim();
    const auto cues = getCues();

    for (int i = 0; i < (int) cues.size(); ++i)
        if (cues[(size_t) i]->number == wanted)
            return i;

    return -1;
}

juce::String ShowSession::getSectionPath (int cueIndex) const
{
    // Walk the rows keeping the open sections, one per depth.
    juce::StringArray path;

    for (auto& row : getRunningOrderRows())
    {
        while (path.size() > row.depth)
            path.remove (path.size() - 1);

        if (row.item->kind == RunItem::Kind::section)
            path.add (row.item->name);
        else if (row.cueIndex == cueIndex)
            return path.joinIntoString (" / ");
    }

    return {};
}

void ShowSession::setShowTemplate (ShowTemplate t)
{
    if (show.showTemplate != t)
    {
        show.showTemplate = t;
        changed (true);
    }
}

void ShowSession::renumberCues()
{
    // Always 1..N in playing order; cut cues have no number.
    int number = 1;

    std::function<void (RunItem&)> walk = [&] (RunItem& section)
    {
        for (auto& item : section.children)
        {
            if (item.kind == RunItem::Kind::section)
                walk (item);
            else
                item.number = item.cut ? juce::String() : juce::String (number++);
        }
    };

    walk (show.runningOrder);
}

void ShowSession::runningOrderChanged (const juce::String& keepPlayingId)
{
    renumberCues();
    publishRunningOrder();

    // "Next" continues from the cue that's playing, wherever it is now.
    if (keepPlayingId.isNotEmpty())
    {
        if (const auto index = getCueIndexOf (keepPlayingId); index >= 0)
            player.setCuePosition (index);
    }

    changed (true);
}

juce::String ShowSession::insertCue (const juce::String& afterId)
{
    const auto playingId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();

    // The new cue plays what the cue before it plays (usually the case on a
    // score: the sound continues until the next change).
    juce::String patchId = show.patches.empty() ? juce::String() : show.patches.front().id;
    const auto rows = getRunningOrderRows();

    for (auto& row : rows)
    {
        if (row.item->kind == RunItem::Kind::cue && ! row.item->uniquePatch && row.item->patchId.isNotEmpty())
            patchId = row.item->patchId;

        if (row.item->id == afterId)
            break;
    }

    const auto* patch = show.findPatch (patchId);
    auto cue = RunItem::makeCue ({}, patch != nullptr ? patch->name : juce::String ("Cue"), patchId);
    const auto id = cue.id;

    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (afterId.isEmpty() || ! locateItem (show.runningOrder, afterId, siblings, position))
        show.runningOrder.children.push_back (std::move (cue));
    else if ((*siblings)[(size_t) position].kind == RunItem::Kind::section)
        (*siblings)[(size_t) position].children.insert ((*siblings)[(size_t) position].children.begin(), std::move (cue));
    else
        siblings->insert (siblings->begin() + position + 1, std::move (cue));

    runningOrderChanged (playingId);
    return id;
}

juce::String ShowSession::insertSection (const juce::String& afterId, const juce::String& name, const juce::String& kind)
{
    const auto playingId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();
    auto section = RunItem::makeSection (name, kind);
    const auto id = section.id;

    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (afterId.isEmpty() || ! locateItem (show.runningOrder, afterId, siblings, position))
        show.runningOrder.children.push_back (std::move (section));
    else
        siblings->insert (siblings->begin() + position + 1, std::move (section));

    runningOrderChanged (playingId);
    return id;
}

void ShowSession::deleteItem (const juce::String& id)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (! locateItem (show.runningOrder, id, siblings, position))
        return;

    const auto playingId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();
    const auto removedIndex = getCueIndexOf (id);
    auto removed = std::move ((*siblings)[(size_t) position]);
    siblings->erase (siblings->begin() + position);

    // A section's contents stay where they were, one level up.
    if (removed.kind == RunItem::Kind::section)
        siblings->insert (siblings->begin() + position,
                          std::make_move_iterator (removed.children.begin()),
                          std::make_move_iterator (removed.children.end()));

    renumberCues();
    publishRunningOrder();

    if (id == playingId)
        player.setCuePosition (removedIndex - 1);   // "next" plays the cue that followed
    else if (const auto index = getCueIndexOf (playingId); index >= 0)
        player.setCuePosition (index);

    changed (true);
}

void ShowSession::moveItem (const juce::String& id, int direction)
{
    std::vector<RunItem>* siblings = nullptr;
    int position = 0;

    if (direction == 0 || ! locateItem (show.runningOrder, id, siblings, position))
        return;

    const auto playingId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();
    const auto target = position + (direction < 0 ? -1 : 1);

    if (juce::isPositiveAndBelow (target, (int) siblings->size()))
    {
        auto& neighbour = (*siblings)[(size_t) target];

        if (neighbour.kind == RunItem::Kind::section && (*siblings)[(size_t) position].kind == RunItem::Kind::cue)
        {
            // Into the neighbouring section: at its end coming from below, at its start from above.
            auto item = std::move ((*siblings)[(size_t) position]);
            siblings->erase (siblings->begin() + position);
            auto& section = (*siblings)[(size_t) (direction < 0 ? target : position)];

            if (direction < 0)
                section.children.push_back (std::move (item));
            else
                section.children.insert (section.children.begin(), std::move (item));
        }
        else
        {
            std::swap ((*siblings)[(size_t) position], neighbour);
        }
    }
    else if (auto* owner = ownerOf (show.runningOrder, siblings); owner != nullptr && owner != &show.runningOrder)
    {
        // At the section's edge: out of it, just before or after it.
        const auto ownerId = owner->id;
        auto item = std::move ((*siblings)[(size_t) position]);
        siblings->erase (siblings->begin() + position);

        std::vector<RunItem>* outer = nullptr;
        int ownerPosition = 0;

        if (locateItem (show.runningOrder, ownerId, outer, ownerPosition))
            outer->insert (outer->begin() + ownerPosition + (direction < 0 ? 0 : 1), std::move (item));
    }
    else
    {
        return;   // already first/last
    }

    runningOrderChanged (playingId);
}

void ShowSession::setCut (const juce::String& id, bool cut)
{
    auto* item = findItem (id);

    if (item == nullptr || item->kind != RunItem::Kind::cue || item->cut == cut)
        return;

    const auto playingId = cueAt (currentCue) != nullptr ? cueAt (currentCue)->id : juce::String();
    const auto wasIndex = getCueIndexOf (id);
    item->cut = cut;

    // A cue with its own patch that was cut when the show opened has no chain yet.
    if (! cut && item->uniquePatch && chains.find (keyForCue (*item)) == chains.end())
        rebuildPatch (keyForCue (*item));

    renumberCues();
    publishRunningOrder();

    if (id == playingId && cut)
        player.setCuePosition (wasIndex - 1);
    else if (const auto index = getCueIndexOf (playingId); index >= 0)
        player.setCuePosition (index);

    changed (true);
}

void ShowSession::setItemName (const juce::String& id, const juce::String& name)
{
    if (auto* item = findItem (id); item != nullptr && item->name != name.trim())
    {
        item->name = name.trim();
        changed (true);
    }
}

void ShowSession::setItemNote (const juce::String& id, const juce::String& note)
{
    if (auto* item = findItem (id); item != nullptr && item->note != note.trim())
    {
        item->note = note.trim();
        changed (true);
    }
}

void ShowSession::setItemBarMark (const juce::String& id, const juce::String& barMark)
{
    if (auto* item = findItem (id); item != nullptr && item->barMark != barMark.trim())
    {
        item->barMark = barMark.trim();
        changed (true);
    }
}

void ShowSession::setSectionKind (const juce::String& id, const juce::String& kind)
{
    if (auto* item = findItem (id); item != nullptr && item->kind == RunItem::Kind::section)
    {
        item->sectionKind = kind;
        changed (true);
    }
}

void ShowSession::setItemPatch (const juce::String& id, const juce::String& patchId)
{
    auto* item = findItem (id);

    if (item == nullptr || item->kind != RunItem::Kind::cue || show.findPatch (patchId) == nullptr)
        return;

    item->patchId = patchId;
    item->uniquePatch.reset();
    publishRunningOrder();
    changed (true);
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

    renumberCues();
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
    renumberCues();
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
    renumberCues();
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

void ShowSession::setCueProgramChange (int index, int program)
{
    if (auto* cue = cueAt (index))
    {
        cue->programChange = juce::jlimit (-1, 127, program);
        publishRunningOrder();
        changed (true);
    }
}

void ShowSession::setCueMidiOut (int index, std::vector<MidiOutMessage> messages)
{
    if (auto* cue = cueAt (index))
    {
        cue->midiOut = std::move (messages);
        publishRunningOrder();
        changed (true);
    }
}

void ShowSession::setOwnMidiMappings (bool own, const std::vector<MidiBinding>& startingPoint)
{
    show.ownMidiMappings = own;

    if (own && show.midiBindings.empty())
        show.midiBindings = startingPoint;

    changed (true);
}

void ShowSession::setShowMidiBindings (std::vector<MidiBinding> bindings)
{
    show.midiBindings = std::move (bindings);
    changed (true);
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

//==============================================================================
void ShowSession::setPluginCatalog (std::shared_ptr<PluginCatalog> catalog)
{
    pluginCatalog = std::move (catalog);
}

PluginContext ShowSession::makePluginContext() const
{
    PluginContext context;
    context.catalog = pluginCatalog;
    context.crashedLastTime = crashedPlugins;

    const auto wanted = buildsWanted;
    const auto now = wanted->load();
    context.keepWaiting = [wanted, now] { return wanted->load() == now; };
    return context;
}

void ShowSession::setCrashedPlugins (const juce::StringArray& blockIds)
{
    crashedPlugins = blockIds;
}

void ShowSession::turnPluginBackOn (int blockIndex)
{
    const auto it = chains.find (activePatchId);

    if (it == chains.end() || ! juce::isPositiveAndBelow (blockIndex, (int) it->second->getBlocks().size()))
        return;

    if (auto* plugin = dynamic_cast<PluginBlock*> (it->second->getBlocks()[(size_t) blockIndex].get()))
    {
        crashedPlugins.removeString (plugin->getModelId());

        if (plugin->getFault() == PluginBlock::Fault::crashed)
        {
            rebuildPatch (activePatchId);   // a crashed plugin is never touched again: load a new one
        }
        else
        {
            plugin->turnBackOn();
            sendChangeMessage();
        }
    }
}

std::vector<juce::String> ShowSession::getPluginProblems() const
{
    std::vector<juce::String> list;

    for (const auto& [key, chain] : chains)
    {
        for (const auto& block : chain->getBlocks())
        {
            const auto* plugin = dynamic_cast<const PluginBlock*> (block.get());

            if (plugin == nullptr)
                continue;

            const auto& fault = plugin->getFaultText();

            if (! plugin->isLoaded())
                list.push_back (chain->label + ": " + plugin->getLoadError());
            else if (fault.isNotEmpty())
                list.push_back (chain->label + ": " + plugin->getName() + " " + fault.substring (0, 1).toLowerCase() + fault.substring (1));
        }
    }

    std::sort (list.begin(), list.end());
    return list;
}

juce::StringArray ShowSession::takePluginNotices()
{
    auto taken = pluginNotices;
    pluginNotices.clear();
    return taken;
}

void ShowSession::checkPluginFaults()
{
    // Cheap until a plugin actually faults: one atomic read per call.
    const auto count = PluginBlock::getFaultCount();

    if (count == lastFaultCount)
        return;

    lastFaultCount = count;

    for (const auto& [key, chain] : chains)
    {
        for (const auto& block : chain->getBlocks())
        {
            const auto* plugin = dynamic_cast<const PluginBlock*> (block.get());

            if (plugin == nullptr || plugin->getFault() == PluginBlock::Fault::none
                || plugin->getFault() == PluginBlock::Fault::crashedLastTime)
                continue;

            const auto id = juce::String::toHexString ((juce::pointer_sized_int) plugin) + "/" + juce::String ((int) plugin->getFault());

            if (reportedFaults.insert (id).second)
            {
                const auto* patch = findPatchForKey (key);
                const auto text = (patch != nullptr ? patch->name + ": " : juce::String())
                                  + plugin->getName() + " " + plugin->getFaultText().substring (0, 1).toLowerCase()
                                  + plugin->getFaultText().substring (1);
                problems.add (text);
                pluginNotices.add (text);
            }
        }
    }

    sendChangeMessage();
}

} // namespace encore
