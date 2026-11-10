// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/PreShowCheck.h"

#include <algorithm>

namespace encore
{

namespace
{
    using Status = PreShowCheckResult::Status;
    using Fix = PreShowCheckResult::Fix;

    void forEachCue (const RunItem& item, const std::function<void (const RunItem&)>& fn)
    {
        if (item.kind == RunItem::Kind::cue)
        {
            if (! item.cut)
                fn (item);
            return;
        }

        for (auto& child : item.children)
            forEachCue (child, fn);
    }

    void checkPatchFiles (const PatchModel& patch, const ShowFolder& folder, std::vector<juce::String>& missing)
    {
        for (auto& lane : patch.lanes)
            for (auto& block : lane.blocks)
                if (block.file.isNotEmpty() && ! folder.resolve (block.file).existsAsFile())
                    missing.push_back (patch.name + " / " + block.type + ": " + block.file);
    }

    juce::String megabytes (juce::int64 bytes) { return juce::String (bytes / (1024 * 1024)) + " MB"; }
}

std::vector<juce::String> findMissingFiles (const ShowModel& show, const ShowFolder& folder)
{
    std::vector<juce::String> missing;

    for (auto& patch : show.patches)
        checkPatchFiles (patch, folder, missing);

    forEachCue (show.runningOrder, [&] (const RunItem& cue)
    {
        if (cue.uniquePatch)
            checkPatchFiles (*cue.uniquePatch, folder, missing);
    });

    return missing;
}

std::vector<juce::String> findCuesWithoutPatch (const ShowModel& show)
{
    std::vector<juce::String> result;

    forEachCue (show.runningOrder, [&] (const RunItem& cue)
    {
        if (! cue.uniquePatch && show.findPatch (cue.patchId) == nullptr)
            result.push_back ((cue.number.isNotEmpty() ? cue.number + " " : juce::String()) + cue.name);
    });

    return result;
}

std::vector<PreShowCheckResult> runPreShowChecks (const PreShowFacts& f)
{
    std::vector<PreShowCheckResult> r;
    auto add = [&r] (Status s, juce::String title, juce::String detail, Fix fix = Fix::none)
    {
        r.push_back ({ s, std::move (title), std::move (detail), fix });
    };

    //== Audio =================================================================
    if (f.reconnecting)
        add (Status::problem, "Audio interface lost", "Reconnecting to " + f.deviceName + "... check the USB cable.", Fix::audioSettings);
    else if (! f.deviceOpen)
        add (Status::problem, "No audio interface", "Choose one in Audio settings.", Fix::audioSettings);
    else
    {
        if (! f.callbacksRunning)
            add (Status::problem, "Audio isn't running", f.deviceName + " is open but not processing.", Fix::audioSettings);

        const auto isAsio = f.deviceType.containsIgnoreCase ("ASIO");
        add (isAsio ? Status::ok : Status::warning, "Driver: " + f.deviceType,
             f.deviceName + (isAsio ? juce::String() : juce::String (" - use the interface's ASIO driver for low latency")),
             isAsio ? Fix::none : Fix::audioSettings);

        const auto bufferStatus = f.bufferSize <= 128 ? Status::ok : f.bufferSize <= 256 ? Status::warning : Status::problem;
        add (bufferStatus, "Buffer " + juce::String (f.bufferSize) + " samples at " + juce::String (f.sampleRate / 1000.0, 1) + " kHz",
             "About " + juce::String (f.latencyMs, 1) + " ms round trip"
                 + (bufferStatus == Status::ok ? juce::String() : juce::String (" - 64 or 128 feels best to play")),
             bufferStatus == Status::ok ? Fix::none : Fix::audioSettings);

        if (f.sampleRate > 50000.0)
            add (Status::warning, "High sample rate", "Twice the CPU for no audible gain on stage: 44.1 or 48 kHz is plenty.", Fix::audioSettings);

        const auto cpuPercent = juce::roundToInt (f.cpuLoad * 100.0);
        add (f.cpuLoad < 0.5 ? Status::ok : f.cpuLoad < 0.75 ? Status::warning : Status::problem,
             "CPU " + juce::String (cpuPercent) + " %",
             f.cpuLoad < 0.5 ? juce::String ("Headroom for switching and ringing tails")
                             : juce::String ("Little headroom: switching patches briefly runs two at once. Try a bigger buffer."));

        if (f.overruns > 0 || f.driverDropouts > 0)
            add (Status::warning, "Dropouts since the last reset",
                 juce::String (f.overruns) + " overruns, " + juce::String (juce::jmax (0, f.driverDropouts))
                     + " driver dropouts. Reset, then play through the show's heaviest patches.",
                 Fix::resetCounters);
        else
            add (Status::ok, "No dropouts", "Since the counters were last reset");

        if (f.badBuffers > 0)
            add (Status::warning, "Bad audio caught", juce::String (f.badBuffers)
                     + " buffers of invalid audio were silenced (a block misbehaved, or the driver). Check the log.");
    }

    if (f.muted)
        add (Status::warning, "Output is muted", "Unmute before the show.", Fix::unmute);

    //== Computer ==============================================================
    if (f.onBattery)
        add (Status::problem, "On battery", "Plug in: laptops slow down on battery, which causes dropouts.");
    else if (! f.powerUnknown)
        add (Status::ok, "Mains power", {});

    if (! f.highPriority || ! f.throttlingOff)
        add (Status::warning, "Windows tuning incomplete",
             juce::String (! f.highPriority ? "Couldn't raise Encore's priority. " : "")
                 + (! f.throttlingOff ? "Power throttling is on." : ""));

    if (f.freeDiskBytes >= 0 && f.freeDiskBytes < (juce::int64) 500 * 1024 * 1024)
        add (Status::warning, "Disk almost full", megabytes (f.freeDiskBytes) + " free: autosave and backups need room.");

    //== Show ==================================================================
    if (! f.showHasFolder)
        add (Status::problem, "Show not saved to disk", "Save it to a folder first.", Fix::save);
    else
        add (f.showSaved ? Status::ok : Status::warning, f.showSaved ? "Show saved" : "Unsaved changes",
             f.showSaved ? juce::String() : juce::String ("Autosave runs in a moment, or save now."),
             f.showSaved ? Fix::none : Fix::save);

    if (f.numCues == 0)
        add (Status::warning, "No cues", "The set list is empty: the footswitch has nothing to step through.");

    if (! f.missingFiles.empty())
    {
        juce::StringArray lines;

        for (auto& m : f.missingFiles)
            lines.add (m);

        add (Status::problem, juce::String ((int) f.missingFiles.size()) + " missing file(s)",
             "These amps/IRs won't sound right: " + lines.joinIntoString ("; "));
    }

    if (! f.cuesWithoutPatch.empty())
    {
        juce::StringArray lines;

        for (auto& c : f.cuesWithoutPatch)
            lines.add (c);

        add (Status::problem, "Cues without a patch", lines.joinIntoString (", "));
    }

    //== MIDI ==================================================================
    juce::StringArray notConnected;

    for (auto& name : f.midiInputsEnabled)
        if (! f.midiInputsConnected.contains (name))
            notConnected.add (name);

    if (! notConnected.isEmpty())
        add (Status::problem, "Foot controller not connected", notConnected.joinIntoString (", "), Fix::midiSettings);
    else if (f.midiInputsEnabled.isEmpty())
        add (Status::warning, "No foot controller", "No MIDI input is enabled.", Fix::midiSettings);
    else
        add (Status::ok, "Foot controller: " + f.midiInputsEnabled.joinIntoString (", "), {});

    if (! f.hasNextCueMapping)
        add (Status::warning, "Nothing mapped to Next cue", "Map a footswitch in MIDI settings.", Fix::midiSettings);

    if (f.cuesSendMidi && ! f.midiOutputConnected)
        add (Status::problem, "MIDI output missing", "Some cues send MIDI, but no output is connected.", Fix::midiSettings);

    // Worst first, otherwise in the order above.
    std::stable_sort (r.begin(), r.end(), [] (const auto& a, const auto& b) { return (int) a.status > (int) b.status; });
    return r;
}

} // namespace encore
