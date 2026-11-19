// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "model/Routing.h"
#include "model/ShowFolder.h"
#include "model/ShowModel.h"

namespace encore
{

/** Everything the pre-show check looks at, gathered by the main window. Plain
    values, so the rules below are easy to test. */
struct PreShowFacts
{
    // Audio
    bool deviceOpen = false;
    juce::String deviceType, deviceName;     // "ASIO", "Windows Audio"...
    int bufferSize = 0;
    double sampleRate = 0.0;
    double latencyMs = 0.0;                  // round trip
    bool callbacksRunning = false;
    bool reconnecting = false;
    double cpuLoad = 0.0;                    // 0..1
    int overruns = 0, driverDropouts = 0;
    int badBuffers = 0;                      // NaN/inf caught
    bool muted = false;

    // Computer
    bool onBattery = false, powerUnknown = false;
    bool highPriority = true, throttlingOff = true;
    juce::int64 freeDiskBytes = -1;          // on the show's drive, -1 = unknown

    // Show
    bool showSaved = true;
    bool showHasFolder = true;
    int numCues = 0;
    std::vector<juce::String> missingFiles;  // "Lead / Amp: models/x.nam"
    std::vector<juce::String> cuesWithoutPatch;
    std::vector<juce::String> pluginProblems;   // "Lead: Shimmer isn't installed on this computer"

    // Routing (milestone 11)
    struct PatchRouting
    {
        juce::String name;
        juce::String input;                  // "" = the first
        std::vector<SendModel> sends;
    };

    RoutingConfig routing = makeDefaultRouting();   // this computer's
    std::vector<PatchRouting> patchRouting;  // the patches the show plays
    juce::StringArray unalignedPatches;      // split paths too far apart to line up
    int deviceInputChannels = -1, deviceOutputChannels = -1;   // -1: no device
    juce::uint64 switchedOnInputs = ~(juce::uint64) 0, switchedOnOutputs = ~(juce::uint64) 0;   // bit n: channel n + 1

    // MIDI
    juce::StringArray midiInputsEnabled, midiInputsConnected;
    bool hasNextCueMapping = false;
    bool cuesSendMidi = false, midiOutputConnected = false;
};

struct PreShowCheckResult
{
    enum class Status { ok, warning, problem };
    enum class Fix { none, audioSettings, midiSettings, pluginSettings, save, resetCounters, unmute,
                     routing, unmuteOutput };

    Status status = Status::ok;
    juce::String title, detail;
    Fix fix = Fix::none;
    juce::String target;                     // unmuteOutput: the output's name
};

/** The checks, worst first. */
std::vector<PreShowCheckResult> runPreShowChecks (const PreShowFacts&);

/** Files a show refers to that aren't there (amp captures, IRs). */
std::vector<juce::String> findMissingFiles (const ShowModel&, const ShowFolder&);

/** Cues whose patch doesn't exist. */
std::vector<juce::String> findCuesWithoutPatch (const ShowModel&);

/** The library's patches and the cues' own (not cut), with their routing. */
std::vector<PreShowFacts::PatchRouting> describePatchRouting (const ShowModel&);

/** "A", "A and B", "A, B and C". */
juce::String joinNames (const juce::StringArray& names);

} // namespace encore
