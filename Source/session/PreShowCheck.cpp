// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/PreShowCheck.h"

#include <algorithm>
#include <map>

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

std::vector<PreShowFacts::PatchRouting> describePatchRouting (const ShowModel& show)
{
    std::vector<PreShowFacts::PatchRouting> list;

    auto add = [&list] (const PatchModel& p) { list.push_back ({ p.name, p.inputName(), p.sends }); };

    for (auto& patch : show.patches)
        add (patch);

    forEachCue (show.runningOrder, [&] (const RunItem& cue)
    {
        if (cue.uniquePatch)
            add (*cue.uniquePatch);
    });

    return list;
}

juce::String joinNames (const juce::StringArray& names)
{
    if (names.size() <= 1)
        return names.joinIntoString ({});

    juce::StringArray first (names);
    first.removeRange (names.size() - 1, 1);
    return first.joinIntoString (", ") + " and " + names[names.size() - 1];
}

namespace
{
    juce::String quoted (const juce::String& name)
    {
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + name + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d"));
    }

    juce::String plural (int n, const char* one, const char* many)
    {
        return juce::String (n) + " " + (n == 1 ? one : many);
    }

    /** The interface outputs an output plays on (1-based); "all" = every one. */
    std::vector<int> channelsOf (const RoutingOutput& out, int deviceOutputs)
    {
        if (! out.isAllOutputs())
            return out.channels;

        std::vector<int> all;

        for (int ch = 1; ch <= juce::jmax (2, deviceOutputs); ++ch)
            all.push_back (ch);

        return all;
    }

    void checkRouting (const PreShowFacts& f, const std::function<void (Status, juce::String, juce::String, Fix, juce::String)>& add)
    {
        const auto& rig = f.routing;
        bool anyIssue = false;
        auto issue = [&] (Status s, juce::String title, juce::String detail, Fix fix, juce::String target = {})
        {
            anyIssue = true;
            add (s, std::move (title), std::move (detail), fix, std::move (target));
        };

        // Names the show uses that this computer doesn't have.
        juce::StringArray outputsMissing, inputsMissing;
        std::map<juce::String, juce::StringArray> sendersOf, playersOf;

        for (auto& p : f.patchRouting)
        {
            for (auto& send : p.sends)
            {
                if (! send.on || rig.findOutput (send.output) != nullptr)
                    continue;

                if (! outputsMissing.contains (send.output, true))
                    outputsMissing.add (send.output);

                sendersOf[send.output.toLowerCase()].addIfNotAlreadyThere (p.name);
            }

            if (p.input.trim().isNotEmpty() && rig.findInput (p.input) == nullptr)
            {
                if (! inputsMissing.contains (p.input, true))
                    inputsMissing.add (p.input);

                playersOf[p.input.toLowerCase()].addIfNotAlreadyThere (p.name);
            }
        }

        for (auto& name : outputsMissing)
        {
            const auto& senders = sendersOf[name.toLowerCase()];
            issue (Status::problem, "Output " + quoted (name) + " isn't on this computer",
                   joinNames (senders) + (senders.size() == 1 ? " sends" : " send")
                       + " to it. Add it in Routing, or send " + (senders.size() == 1 ? "that patch" : "those patches")
                       + " somewhere else.",
                   Fix::routing);
        }

        const auto& first = rig.inputFor ({});

        for (auto& name : inputsMissing)
        {
            const auto& players = playersOf[name.toLowerCase()];
            issue (Status::problem, "Input " + quoted (name) + " isn't on this computer",
                   joinNames (players) + (players.size() == 1 ? " plays " : " play ") + first.name
                       + " (input " + juce::String (first.channel) + ") instead.",
                   Fix::routing);
        }

        // Channels the interface doesn't have.
        if (f.deviceOpen && f.deviceOutputChannels >= 0)
        {
            for (auto& out : rig.outputs)
            {
                if (out.isAllOutputs())
                    continue;

                const auto missing = std::any_of (out.channels.begin(), out.channels.end(),
                                                  [&] (int ch) { return ch > f.deviceOutputChannels; });

                if (missing)
                    issue (Status::problem, "Output " + quoted (out.name) + " needs output"
                               + (out.channels.size() == 2 ? "s " + juce::String (out.channels[0]) + "/" + juce::String (out.channels[1])
                                                           : " " + juce::String (out.channels[0])),
                           f.deviceName + " has " + plural (f.deviceOutputChannels, "output.", "outputs."),
                           Fix::routing);
            }
        }

        if (f.deviceOpen && f.deviceInputChannels >= 0)
            for (auto& in : rig.inputs)
                if (in.channel > f.deviceInputChannels)
                    issue (Status::problem, "Input " + quoted (in.name) + " needs input " + juce::String (in.channel),
                           f.deviceName + " has " + plural (f.deviceInputChannels, "input.", "inputs."),
                           Fix::routing);

        // Channels it has, but switched off in Audio settings: silent.
        auto isOn = [] (juce::uint64 mask, int channel)
        {
            return channel > 64 || ((mask >> (channel - 1)) & 1u) != 0;
        };

        if (f.deviceOpen)
        {
            for (auto& out : rig.outputs)
                for (auto ch : out.channels)
                    if (ch <= f.deviceOutputChannels && ! isOn (f.switchedOnOutputs, ch))
                    {
                        issue (Status::problem, "Output " + quoted (out.name) + " uses output " + juce::String (ch) + ", which is switched off",
                               "Switch it on under Active channels in Audio settings.", Fix::audioSettings);
                        break;
                    }

            for (auto& in : rig.inputs)
                if (in.channel <= f.deviceInputChannels && ! isOn (f.switchedOnInputs, in.channel))
                    issue (Status::problem, "Input " + quoted (in.name) + " uses input " + juce::String (in.channel) + ", which is switched off",
                           "Switch it on under Active channels in Audio settings.", Fix::audioSettings);
        }

        // Muted outputs.
        for (auto& out : rig.outputs)
            if (out.mute)
                issue (Status::warning, "Output " + quoted (out.name) + " is muted",
                       "Unmute it before the show if it should play.", Fix::unmuteOutput, out.name);

        // Patches that reach no output.
        for (auto& p : f.patchRouting)
        {
            const auto reaches = std::any_of (rig.outputs.begin(), rig.outputs.end(), [&] (const RoutingOutput& out)
            {
                if (out.mute)
                    return false;

                for (auto& send : p.sends)
                    if (routing::sameName (send.output, out.name))
                        return send.on;

                return true;   // not mentioned: 0 dB
            });

            if (! reaches)
                issue (Status::warning, quoted (p.name) + " sends to nothing",
                       "Every send is off or goes to a muted output.", Fix::routing);
        }

        // Outputs on the same channel (they add up).
        juce::StringArray shared;

        for (size_t a = 0; a < rig.outputs.size(); ++a)
            for (size_t b = a + 1; b < rig.outputs.size(); ++b)
            {
                const auto ca = channelsOf (rig.outputs[a], f.deviceOutputChannels);
                const auto cb = channelsOf (rig.outputs[b], f.deviceOutputChannels);

                for (auto ch : ca)
                    if (std::find (cb.begin(), cb.end(), ch) != cb.end())
                    {
                        shared.add (quoted (rig.outputs[a].name) + " and " + quoted (rig.outputs[b].name)
                                    + " both use output " + juce::String (ch) + ".");
                        break;
                    }
            }

        if (! shared.isEmpty())
            issue (Status::warning, shared.size() == 1 ? "Outputs share a channel" : "Outputs share channels",
                   shared.joinIntoString (" "), Fix::routing);

        for (auto& name : f.unalignedPatches)
            issue (Status::warning, quoted (name) + ": the paths can't be lined up",
                   "Path A and path B differ by more than a plugin's latency Encore can make up (170 ms): they may sound out of time.",
                   Fix::none);

        // All good, and more than the defaults: say so.
        if (! anyIssue && (rig.inputs.size() > 1 || rig.outputs.size() > 1))
            add (Status::ok, "Routing",
                 plural ((int) rig.inputs.size(), "input", "inputs") + ", " + plural ((int) rig.outputs.size(), "output", "outputs")
                     + (f.deviceOpen ? ", all on this interface" : ""),
                 Fix::none, {});
    }
}

std::vector<PreShowCheckResult> runPreShowChecks (const PreShowFacts& f)
{
    std::vector<PreShowCheckResult> r;
    auto add = [&r] (Status s, juce::String title, juce::String detail, Fix fix = Fix::none)
    {
        r.push_back ({ s, std::move (title), std::move (detail), fix, {} });
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

    if (! f.pluginProblems.empty())
    {
        juce::StringArray lines;

        for (auto& p : f.pluginProblems)
            lines.add (p);

        add (Status::problem, juce::String ((int) f.pluginProblems.size()) + (f.pluginProblems.size() == 1 ? " plugin" : " plugins") + " not playing",
             "The signal passes through them dry: " + lines.joinIntoString ("; "), Fix::pluginSettings);
    }

    if (! f.cuesWithoutPatch.empty())
    {
        juce::StringArray lines;

        for (auto& c : f.cuesWithoutPatch)
            lines.add (c);

        add (Status::problem, "Cues without a patch", lines.joinIntoString (", "));
    }

    //== Routing ===============================================================
    checkRouting (f, [&r] (Status s, juce::String title, juce::String detail, Fix fix, juce::String target)
    {
        r.push_back ({ s, std::move (title), std::move (detail), fix, std::move (target) });
    });

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
