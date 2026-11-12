// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <map>

#include "model/MidiMapping.h"
#include "model/StageLayout.h"
#include <functional>
#include <optional>
#include <vector>

namespace encore
{

/** The show as saved on disk: plain values, no audio objects.

    One structure covers every kind of gig. A show is a tree of optional
    sections (acts, scenes, songs...) that ends in cues; each cue points at a
    patch in the show's patch library (or carries its own "unique" patch).
    A band set list is just a flat list of songs with one cue each; a musical
    uses acts, numbers and hundreds of numbered cues. See the architecture doc.

    IDs are stable for ever (MIDI maps, backups and layouts refer to them);
    names and cue numbers are free to change.
*/

/** A fresh, unique, stable ID. */
juce::String newId();

/** The block type of a plugin (VST3) block. */
inline constexpr const char* pluginBlockType = "plugin";

/** Split paths (milestone 11): a "split" block in the main lane starts two
    parallel paths; the blocks after it up to the "merge" block are path A,
    lanes[1] is path B. The merge mixes them (level and pan each) and the main
    lane goes on after it. The split's on/off is path B on/off. */
inline constexpr const char* splitBlockType = "split";
inline constexpr const char* mergeBlockType = "merge";

namespace mergeParams
{
    inline constexpr const char* levelA = "levelA";   // -inf..+6 dB, 0
    inline constexpr const char* panA   = "panA";     // -100 (L)..+100 (R), 0
    inline constexpr const char* levelB = "levelB";
    inline constexpr const char* panB   = "panB";
}

//==============================================================================
/** Which plugin a "plugin" block uses: enough to find the same plugin on
    another computer (where it may be installed somewhere else). */
struct PluginRef
{
    juce::String format;                     // "VST3"
    juce::String name;
    juce::String maker;
    int uid = 0;                             // the format's unique ID for the plugin
    juce::String version;
    juce::String file;                       // where it was installed when saved (a hint)

    bool isEmpty() const noexcept { return name.isEmpty(); }
    bool operator== (const PluginRef&) const = default;
};

struct BlockModel
{
    juce::String id;
    juce::String type;                       // "nam", "cab", "gate", "eq"...
    bool enabled = true;
    std::map<juce::String, float> params;    // parameter id -> value, in real units
    juce::String file;                       // show-relative ("models/x.nam") or absolute until collected
    int stomp = 0;                           // which stomp switch toggles it (1..8), 0 = none
    std::map<juce::String, ExpressionAssignment> expression;   // parameter id -> pedal and range
    PluginRef plugin;                        // "plugin" blocks: which plugin
    juce::String state;                      // "plugin" blocks: the plugin's own settings, base64

    bool operator== (const BlockModel&) const = default;
};

/** A chain of blocks. lanes[0] is the patch's chain (its input names the
    computer's input it plays, "" = the first); lanes[1] is path B of a split. */
struct LaneModel
{
    juce::String input;                      // lane 0: the input's name ("" = this computer's first input)
    juce::String output;                     // unused (sends name the outputs); kept for old files
    std::vector<BlockModel> blocks;

    bool operator== (const LaneModel&) const = default;
};

enum class Spillover { tails, fullChain };

/** How loud a patch plays into one of the computer's outputs, by name. */
struct SendModel
{
    juce::String output;                     // the output's name
    float levelDb = 0.0f;                    // -inf..+6 dB
    bool on = true;                          // off = -inf

    bool operator== (const SendModel&) const = default;
};

struct PatchModel
{
    juce::String id;
    juce::String name;
    juce::uint32 colour = 0xff4f9dff;
    Spillover spillover = Spillover::tails;
    float fadeInMs = 20.0f;                  // switching to this patch: how quickly it comes in
    float ringOutMs = 150.0f;                // switching away: how long its amp keeps getting the guitar
    std::vector<LaneModel> lanes { LaneModel{} };
    std::vector<SendModel> sends;            // by output name; an output not listed gets 0 dB

    LaneModel& mainLane()             { if (lanes.empty()) lanes.emplace_back(); return lanes.front(); }
    const LaneModel& mainLane() const { static const LaneModel none; return lanes.empty() ? none : lanes.front(); }

    /** The input's name ("" = the computer's first input). */
    const juce::String& inputName() const    { return mainLane().input; }

    /** The send to an output: as listed, else 0 dB on. */
    SendModel sendFor (const juce::String& outputName) const;

    /** Sets (or adds) the send to an output. */
    void setSend (const juce::String& outputName, float levelDb, bool on);

    /** Split paths: the split and merge blocks' places in the main lane (-1 = none). */
    int splitIndex() const;
    int mergeIndex() const;
    bool hasSplit() const                    { return splitIndex() >= 0; }

    /** Path B (lanes[1]); null without a split. */
    LaneModel* pathB()                       { return hasSplit() && lanes.size() > 1 ? &lanes[1] : nullptr; }
    const LaneModel* pathB() const           { return hasSplit() && lanes.size() > 1 ? &lanes[1] : nullptr; }

    /** Whether it uses anything of routing (an input name, sends, a split). */
    bool usesRouting() const;

    bool operator== (const PatchModel&) const = default;
};

enum class SamePatchBehaviour { keep, reset };

/** A node in the running order: a section (act, scene, song...) or a cue. */
struct RunItem
{
    enum class Kind { section, cue };

    Kind kind = Kind::cue;
    juce::String id;
    juce::String name;                       // section title / song or scene name
    juce::String note;                       // free text ("watch conductor")
    bool cut = false;                        // kept in the file, skipped in the show

    // Sections
    juce::String sectionKind;                // "act", "scene", "song", "number"...
    std::vector<RunItem> children;

    // Cues
    juce::String number;                     // "42.5" (display; renumbering never breaks IDs)
    juce::String barMark;                    // "bar 112 / C"
    juce::String patchId;                    // shared patch from the library...
    std::optional<PatchModel> uniquePatch;   // ...or this cue's own ("make unique")
    SamePatchBehaviour samePatch = SamePatchBehaviour::keep;
    int programChange = -1;                  // optional direct recall, -1 = none
    std::vector<MidiOutMessage> midiOut;     // sent to other gear when the cue starts
    juce::String layoutId;                   // stage layout for this cue, empty = the show's default

    static RunItem makeSection (const juce::String& name, const juce::String& sectionKind);
    static RunItem makeCue (const juce::String& number, const juce::String& name, const juce::String& patchId);

    bool operator== (const RunItem&) const = default;
};

enum class ShowTemplate { band, musical, worship, solo };

struct ShowModel
{
    // 2: plugin blocks (milestone 10). 3: routing (milestone 11): patch inputs,
    // sends, split paths. A show is saved with the oldest version that can hold
    // it, so older Encore versions keep opening shows that don't need more.
    static constexpr int currentSchemaVersion = 3;

    juce::String id;
    juce::String name;
    ShowTemplate showTemplate = ShowTemplate::band;
    std::vector<PatchModel> patches;
    RunItem runningOrder = RunItem::makeSection ({}, {});   // the root section

    bool ownMidiMappings = false;            // use these instead of the computer's mappings
    std::vector<MidiBinding> midiBindings;

    StageSettings stage;

    PatchModel* findPatch (const juce::String& patchId);
    const PatchModel* findPatch (const juce::String& patchId) const;

    /** Whether any patch (library or a cue's own) has a plugin block. */
    bool usesPlugins() const;

    /** Whether any patch (library or a cue's own) uses routing. */
    bool usesRouting() const;

    /** The schema version this show needs: the oldest that can hold it. */
    int requiredSchemaVersion() const { return usesRouting() ? 3 : usesPlugins() ? 2 : 1; }

    /** Every input and output name the show's patches use (in order, once each,
        ignoring case), e.g. to find the ones this computer doesn't have. */
    juce::StringArray inputNamesUsed() const;
    juce::StringArray outputNamesUsed() const;

    /** Calls fn for every patch: the library's, then the cues' own. */
    void forEachPatch (const std::function<void (const PatchModel&)>& fn) const;
    void forEachPatch (const std::function<void (PatchModel&)>& fn);

    bool operator== (const ShowModel&) const = default;
};

//==============================================================================
/** The words the running order uses, by kind of show: a band plays songs from
    a set list, a musical runs cues from a cue list. Same data, same features. */
struct ShowWords
{
    juce::String list;          // "Set list" / "Cue list"
    juce::String item;          // "song" / "cue"
    juce::String items;         // "songs" / "cues"
    juce::String sectionKind;   // default for a new section: "set" / "scene"
    juce::String newSection;    // "New set" / "New scene"

    juce::String Item() const   { return item.substring (0, 1).toUpperCase() + item.substring (1); }
};

ShowWords wordsFor (ShowTemplate);
juce::String describe (ShowTemplate);   // "Band", "Musical / theatre"...

//==============================================================================
/** A new show with one ready-to-play patch, so a first start makes sound. */
ShowModel makeDefaultShow (const juce::String& name);

/** The default rig for a new patch: Amp -> Cab -> Gate (detect guitar) -> EQ. */
PatchModel makeDefaultPatch (const juce::String& name);

/** A copy of a patch with fresh IDs (for "Duplicate" and "make unique"). */
PatchModel duplicatePatch (const PatchModel& original, const juce::String& newName);

/** Makes a patch's split paths well-formed: at most one split, a merge after
    it (added at the end if missing), no merge without a split, path B present
    with a split and no extra lanes. Run on load and after edits. */
void normaliseSplit (PatchModel& patch);

/** A new split in the main lane before the block at index (0..size): split,
    then the merge right after it, so path A and path B start empty. Does
    nothing (returns false) when the patch already has one. */
bool insertSplit (PatchModel& patch, int index);

/** Removes the split: path A's blocks stay in the main lane, path B's are dropped. */
void removeSplit (PatchModel& patch);

} // namespace encore
