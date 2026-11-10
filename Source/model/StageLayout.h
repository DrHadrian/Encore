// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace encore
{

/** What the stage view can show. Each is a rectangle on a grid. */
enum class StageWidgetType
{
    currentCue,   // number and name of the cue playing, big
    nextCue,      // what the next press brings
    patchName,    // the patch playing
    cueNote,      // the cue's note and bar mark ("watch conductor", "bar 112")
    setList,      // the running order around the current cue
    stomps,       // the playing patch's stomp switches, on or off
    pedals,       // expression pedal positions
    tuner,        // tuner (without one, the tuner covers the screen when open)
    meters,       // input and output level
    clock,        // time of day
    health,       // CPU, overruns, power: is everything fine?
    panicButton,
    lockButton,
    nextButton,   // big touch/click targets (also for a touch screen)
    previousButton,
    text          // fixed text, e.g. a reminder
};

constexpr int stageGridColumns = 24;
constexpr int stageGridRows = 16;

struct StageWidget
{
    StageWidgetType type = StageWidgetType::text;
    int x = 0, y = 0, width = 4, height = 2;   // grid cells
    float textScale = 1.0f;                    // 0.5..3: bigger for far away
    juce::String text;                         // text widget only

    bool operator== (const StageWidget&) const = default;
};

struct StageLayout
{
    juce::String id;
    juce::String name;
    std::vector<StageWidget> widgets;

    bool operator== (const StageLayout&) const = default;
};

enum class PanicBehaviour { recover, latch };

/** A show's stage settings: layouts and how the tuner and panic behave. */
struct StageSettings
{
    std::vector<StageLayout> layouts;        // empty = the built-in default
    juce::String defaultLayoutId;            // used by cues without their own
    bool tunerMutes = true;
    float referencePitch = 440.0f;
    PanicBehaviour panic = PanicBehaviour::recover;

    /** The layout with this id, else the default, else the first, else null. */
    const StageLayout* findLayout (const juce::String& id) const;

    bool operator== (const StageSettings&) const = default;
};

/** A layout that works out of the box on a laptop or a monitor. */
StageLayout makeDefaultStageLayout();

juce::String toString (StageWidgetType);
StageWidgetType stageWidgetTypeFromString (const juce::String&, bool& known);
juce::String describe (StageWidgetType);   // "Current cue"
std::vector<StageWidgetType> allStageWidgetTypes();

/** Keeps a widget on the grid with at least one cell. */
StageWidget clampToGrid (StageWidget);

juce::var stageToVar (const StageSettings&);
StageSettings stageFromVar (const juce::var&);

} // namespace encore
