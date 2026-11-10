// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <map>
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

//==============================================================================
struct BlockModel
{
    juce::String id;
    juce::String type;                       // "nam", "cab", "gate", "eq"...
    bool enabled = true;
    std::map<juce::String, float> params;    // parameter id -> value, in real units
    juce::String file;                       // show-relative ("models/x.nam") or absolute until collected

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

    static RunItem makeSection (const juce::String& name, const juce::String& sectionKind);
    static RunItem makeCue (const juce::String& number, const juce::String& name, const juce::String& patchId);

    bool operator== (const RunItem&) const = default;
};

enum class ShowTemplate { band, musical, worship, solo };

struct ShowModel
{
    static constexpr int currentSchemaVersion = 1;

    juce::String id;
    juce::String name;
    ShowTemplate showTemplate = ShowTemplate::band;
    std::vector<PatchModel> patches;
    RunItem runningOrder = RunItem::makeSection ({}, {});   // the root section

    PatchModel* findPatch (const juce::String& patchId);
    const PatchModel* findPatch (const juce::String& patchId) const;

    bool operator== (const ShowModel&) const = default;
};

//==============================================================================
/** A new show with one ready-to-play patch, so a first start makes sound. */
ShowModel makeDefaultShow (const juce::String& name);

/** The default rig for a new patch: Amp -> Cab -> Gate (detect guitar) -> EQ. */
PatchModel makeDefaultPatch (const juce::String& name);

/** A copy of a patch with fresh IDs (for "Duplicate" and "make unique"). */
PatchModel duplicatePatch (const PatchModel& original, const juce::String& newName);

} // namespace encore
