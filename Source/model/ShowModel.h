// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <map>

#include "model/MidiMapping.h"
#include "model/StageLayout.h"
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

/** One input -> blocks -> output path. The MVP has one per patch; extra lanes
    (two instruments at once, parallel amps) come later without a format change. */
struct LaneModel
{
    juce::String input;                      // "" = the guitar input from device settings
    juce::String output;                     // "" = main output; later a bus name ("FOH", "IEM")
    std::vector<BlockModel> blocks;

    bool operator== (const LaneModel&) const = default;
};

enum class Spillover { tails, fullChain };

struct PatchModel
{
    juce::String id;
    juce::String name;
    juce::uint32 colour = 0xff4f9dff;
    Spillover spillover = Spillover::tails;
    float fadeInMs = 20.0f;                  // switching to this patch: how quickly it comes in
    float ringOutMs = 150.0f;                // switching away: how long its amp keeps getting the guitar
    std::vector<LaneModel> lanes { LaneModel{} };

    LaneModel& mainLane()             { if (lanes.empty()) lanes.emplace_back(); return lanes.front(); }
    const LaneModel& mainLane() const { static const LaneModel none; return lanes.empty() ? none : lanes.front(); }

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
    // 2: plugin blocks (milestone 10). A show without plugins is still saved
    // as 1, so older Encore versions keep opening it.
    static constexpr int currentSchemaVersion = 2;

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

    /** The schema version this show needs: the oldest that can hold it. */
    int requiredSchemaVersion() const { return usesPlugins() ? 2 : 1; }

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

} // namespace encore
