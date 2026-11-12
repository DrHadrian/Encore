// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/ShowJson.h"
#include "model/Routing.h"

namespace encore::json
{

namespace
{
    using Obj = juce::DynamicObject;

    juce::var object() { return juce::var (new Obj()); }

    void set (juce::var& o, const juce::Identifier& key, const juce::var& value)
    {
        o.getDynamicObject()->setProperty (key, value);
    }

    juce::var get (const juce::var& o, const char* key)
    {
        return o.getProperty (juce::Identifier (key), {});
    }

    juce::String str (const juce::var& o, const char* key, const juce::String& fallback = {})
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : v.toString();
    }

    bool flag (const juce::var& o, const char* key, bool fallback)
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : (bool) v;
    }

    bool hasSend (const PatchModel& patch, const juce::String& output)
    {
        for (const auto& s : patch.sends)
            if (routing::sameName (s.output, output))
                return true;

        return false;
    }

    int integer (const juce::var& o, const char* key, int fallback)
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : (int) v;
    }

    template <typename Fn>
    void forEachIn (const juce::var& o, const char* key, Fn&& fn)
    {
        if (auto* array = get (o, key).getArray())
            for (auto& item : *array)
                fn (item);
    }

    //==========================================================================
    const char* toString (Spillover s)          { return s == Spillover::fullChain ? "fullChain" : "tails"; }
    const char* toString (SamePatchBehaviour b) { return b == SamePatchBehaviour::reset ? "reset" : "keep"; }

    const char* toString (ShowTemplate t)
    {
        switch (t)
        {
            case ShowTemplate::musical: return "musical";
            case ShowTemplate::worship: return "worship";
            case ShowTemplate::solo:    return "solo";
            case ShowTemplate::band:    break;
        }
        return "band";
    }

    ShowTemplate templateFrom (const juce::String& s)
    {
        if (s == "musical") return ShowTemplate::musical;
        if (s == "worship") return ShowTemplate::worship;
        if (s == "solo")    return ShowTemplate::solo;
        return ShowTemplate::band;
    }

    //==========================================================================
    juce::var blockToVar (const BlockModel& b)
    {
        auto o = object();
        set (o, "id", b.id);
        set (o, "type", b.type);
        set (o, "enabled", b.enabled);

        auto params = object();
        for (auto& [key, value] : b.params)
            set (params, juce::Identifier (key), value);

        set (o, "params", params);

        if (b.file.isNotEmpty())
            set (o, "file", b.file);

        if (b.stomp > 0)
            set (o, "stomp", b.stomp);

        if (b.type == pluginBlockType || ! b.plugin.isEmpty())
        {
            auto plugin = object();
            set (plugin, "format", b.plugin.format);
            set (plugin, "name", b.plugin.name);
            set (plugin, "maker", b.plugin.maker);
            set (plugin, "uid", b.plugin.uid);
            set (plugin, "version", b.plugin.version);
            set (plugin, "file", b.plugin.file);
            set (o, "plugin", plugin);

            if (b.state.isNotEmpty())
                set (o, "state", b.state);
        }

        if (! b.expression.empty())
        {
            auto expression = object();

            for (auto& [paramId, e] : b.expression)
            {
                auto assignment = object();
                set (assignment, "pedal", e.pedal);
                set (assignment, "min", e.min);
                set (assignment, "max", e.max);
                set (expression, juce::Identifier (paramId), assignment);
            }

            set (o, "expression", expression);
        }

        return o;
    }

    BlockModel blockFromVar (const juce::var& o)
    {
        BlockModel b;
        b.id = str (o, "id", newId());
        b.type = str (o, "type");
        b.enabled = flag (o, "enabled", true);
        b.file = str (o, "file");
        b.stomp = juce::jlimit (0, maxStomps, integer (o, "stomp", 0));
        b.state = str (o, "state");

        if (const auto plugin = get (o, "plugin"); plugin.isObject())
        {
            b.plugin.format = str (plugin, "format");
            b.plugin.name = str (plugin, "name");
            b.plugin.maker = str (plugin, "maker");
            b.plugin.uid = integer (plugin, "uid", 0);
            b.plugin.version = str (plugin, "version");
            b.plugin.file = str (plugin, "file");
        }

        if (auto* expression = get (o, "expression").getDynamicObject())
            for (auto& e : expression->getProperties())
                b.expression[e.name.toString()] = { juce::jlimit (1, maxExpressionPedals, (int) e.value.getProperty ("pedal", 1)),
                                                    (float) (double) e.value.getProperty ("min", 0.0),
                                                    (float) (double) e.value.getProperty ("max", 1.0) };

        if (auto* params = get (o, "params").getDynamicObject())
            for (auto& p : params->getProperties())
                b.params[p.name.toString()] = (float) (double) p.value;

        return b;
    }

    juce::var runItemToVar (const RunItem& item)
    {
        auto o = object();
        set (o, "kind", item.kind == RunItem::Kind::section ? "section" : "cue");
        set (o, "id", item.id);

        if (item.name.isNotEmpty()) set (o, "name", item.name);
        if (item.note.isNotEmpty()) set (o, "note", item.note);
        if (item.cut)               set (o, "cut", true);

        if (item.kind == RunItem::Kind::section)
        {
            if (item.sectionKind.isNotEmpty())
                set (o, "sectionKind", item.sectionKind);

            juce::Array<juce::var> children;
            for (auto& child : item.children)
                children.add (runItemToVar (child));

            set (o, "children", children);
        }
        else
        {
            set (o, "number", item.number);
            if (item.barMark.isNotEmpty()) set (o, "barMark", item.barMark);
            if (item.patchId.isNotEmpty()) set (o, "patch", item.patchId);
            if (item.uniquePatch)          set (o, "uniquePatch", patchToVar (*item.uniquePatch));
            set (o, "samePatch", toString (item.samePatch));
            if (item.programChange >= 0)   set (o, "programChange", item.programChange);

            if (item.layoutId.isNotEmpty()) set (o, "layout", item.layoutId);

            if (! item.midiOut.empty())
            {
                juce::Array<juce::var> out;

                for (auto& m : item.midiOut)
                {
                    auto message = object();
                    set (message, "kind", m.kind == MidiOutMessage::Kind::programChange ? "pc" : "cc");
                    set (message, "channel", m.channel);
                    set (message, "number", m.number);
                    set (message, "value", m.value);
                    out.add (message);
                }

                set (o, "midiOut", out);
            }
        }

        return o;
    }

    RunItem runItemFromVar (const juce::var& o)
    {
        RunItem item;
        item.kind = str (o, "kind") == "section" ? RunItem::Kind::section : RunItem::Kind::cue;
        item.id = str (o, "id", newId());
        item.name = str (o, "name");
        item.note = str (o, "note");
        item.cut = flag (o, "cut", false);

        if (item.kind == RunItem::Kind::section)
        {
            item.sectionKind = str (o, "sectionKind");
            forEachIn (o, "children", [&] (const juce::var& c) { item.children.push_back (runItemFromVar (c)); });
        }
        else
        {
            item.number = str (o, "number");
            item.barMark = str (o, "barMark");
            item.patchId = str (o, "patch");

            if (const auto unique = get (o, "uniquePatch"); unique.isObject())
                item.uniquePatch = patchFromVar (unique);

            item.samePatch = str (o, "samePatch") == "reset" ? SamePatchBehaviour::reset : SamePatchBehaviour::keep;
            item.programChange = integer (o, "programChange", -1);
            item.layoutId = str (o, "layout");

            forEachIn (o, "midiOut", [&] (const juce::var& m)
            {
                MidiOutMessage message;
                message.kind = str (m, "kind") == "cc" ? MidiOutMessage::Kind::controlChange : MidiOutMessage::Kind::programChange;
                message.channel = juce::jlimit (1, 16, integer (m, "channel", 1));
                message.number = juce::jlimit (0, 127, integer (m, "number", 0));
                message.value = juce::jlimit (0, 127, integer (m, "value", 0));
                item.midiOut.push_back (message);
            });
        }

        return item;
    }
}

//==============================================================================
juce::var patchToVar (const PatchModel& patch)
{
    auto o = object();
    set (o, "id", patch.id);
    set (o, "name", patch.name);
    set (o, "colour", juce::String::toHexString ((int) patch.colour));
    set (o, "spillover", toString (patch.spillover));
    set (o, "fadeInMs", patch.fadeInMs);
    set (o, "ringOutMs", patch.ringOutMs);

    juce::Array<juce::var> lanes;

    for (auto& lane : patch.lanes)
    {
        auto l = object();
        if (lane.input.isNotEmpty())  set (l, "input", lane.input);
        if (lane.output.isNotEmpty()) set (l, "output", lane.output);

        juce::Array<juce::var> blocks;
        for (auto& b : lane.blocks)
            blocks.add (blockToVar (b));

        set (l, "blocks", blocks);
        lanes.add (l);
    }

    set (o, "lanes", lanes);

    if (! patch.sends.empty())
    {
        juce::Array<juce::var> sends;

        for (auto& send : patch.sends)
        {
            auto v = object();
            set (v, "output", send.output);
            // -inf has no JSON number; an off send keeps its level for when it's turned back on.
            set (v, "level", juce::jmax (send.levelDb, routing::silentDb));

            if (! send.on)
                set (v, "on", false);

            sends.add (v);
        }

        set (o, "sends", sends);
    }

    return o;
}

PatchModel patchFromVar (const juce::var& o)
{
    PatchModel patch;
    patch.id = str (o, "id", newId());
    patch.name = str (o, "name", "Untitled");
    patch.colour = (juce::uint32) str (o, "colour", "ff4f9dff").getHexValue32();
    patch.spillover = str (o, "spillover") == "fullChain" ? Spillover::fullChain : Spillover::tails;

    if (const auto v = get (o, "fadeInMs"); ! v.isVoid())   patch.fadeInMs = (float) (double) v;
    if (const auto v = get (o, "ringOutMs"); ! v.isVoid())  patch.ringOutMs = (float) (double) v;
    patch.lanes.clear();

    forEachIn (o, "lanes", [&] (const juce::var& l)
    {
        LaneModel lane;
        lane.input = str (l, "input");
        lane.output = str (l, "output");
        forEachIn (l, "blocks", [&] (const juce::var& b) { lane.blocks.push_back (blockFromVar (b)); });
        patch.lanes.push_back (std::move (lane));
    });

    if (patch.lanes.empty())
        patch.lanes.emplace_back();

    forEachIn (o, "sends", [&] (const juce::var& v)
    {
        const auto output = str (v, "output").trim();

        if (output.isEmpty() || hasSend (patch, output))
            return;   // unnamed, or listed twice: the first wins

        const auto level = get (v, "level");
        const auto dB = (level.isDouble() || level.isInt() || level.isInt64()) ? (float) (double) level : 0.0f;
        patch.sends.push_back ({ output, routing::clampLevel (dB), flag (v, "on", true) && dB > routing::silentDb });
    });

    normaliseSplit (patch);
    return patch;
}

juce::String toJson (const ShowModel& show)
{
    auto root = object();
    set (root, "format", "encore-show");
    set (root, "schemaVersion", show.requiredSchemaVersion());
    set (root, "id", show.id);
    set (root, "name", show.name);
    set (root, "template", toString (show.showTemplate));

    juce::Array<juce::var> patches;
    for (auto& p : show.patches)
        patches.add (patchToVar (p));

    set (root, "patches", patches);
    set (root, "runningOrder", runItemToVar (show.runningOrder));

    auto midi = object();
    set (midi, "ownMappings", show.ownMidiMappings);
    set (midi, "bindings", bindingsToVar (show.midiBindings));
    set (root, "midi", midi);
    set (root, "stage", stageToVar (show.stage));

    // Full precision, so values survive a save/load round trip exactly.
    return juce::JSON::toString (root, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine));
}

std::optional<ShowModel> fromJson (const juce::String& text, juce::String& error)
{
    error.clear();
    juce::var root;
    const auto result = juce::JSON::parse (text, root);

    if (result.failed() || ! root.isObject())
    {
        error = "The show file is damaged (" + result.getErrorMessage() + ")";
        return std::nullopt;
    }

    if (str (root, "format") != "encore-show")
    {
        error = "This isn't an Encore show file";
        return std::nullopt;
    }

    const auto version = integer (root, "schemaVersion", 0);

    if (version > ShowModel::currentSchemaVersion)
    {
        error = "This show was saved by a newer version of Encore. Update Encore to open it.";
        return std::nullopt;
    }

    // Migrations from older schema versions go here, one step at a time.

    ShowModel show;
    show.id = str (root, "id", newId());
    show.name = str (root, "name", "Untitled show");
    show.showTemplate = templateFrom (str (root, "template"));

    forEachIn (root, "patches", [&] (const juce::var& p) { show.patches.push_back (patchFromVar (p)); });

    if (const auto order = get (root, "runningOrder"); order.isObject())
        show.runningOrder = runItemFromVar (order);

    show.runningOrder.kind = RunItem::Kind::section;   // the root is always a section

    if (const auto midi = get (root, "midi"); midi.isObject())
    {
        show.ownMidiMappings = flag (midi, "ownMappings", false);
        show.midiBindings = bindingsFromVar (get (midi, "bindings"));
    }

    show.stage = stageFromVar (get (root, "stage"));
    return show;
}

} // namespace encore::json
