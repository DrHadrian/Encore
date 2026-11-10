// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/MidiMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>   // note names

namespace encore
{

namespace
{
    struct ActionName { MidiAction action; const char* id; const char* label; };

    const ActionName actionNames[] = {
        { MidiAction::nextCue,       "nextCue",       "Next cue" },
        { MidiAction::previousCue,   "previousCue",   "Previous cue" },
        { MidiAction::programRecall, "programRecall", "Recall cue by program change" },
        { MidiAction::stomp,         "stomp",         "Stomp" },
        { MidiAction::expression,    "expression",    "Expression" },
        { MidiAction::mute,          "mute",          "Mute" },
        { MidiAction::tuner,         "tuner",         "Tuner" },
        { MidiAction::panic,         "panic",         "Panic" },
    };

    const char* kindId (MidiTrigger::Kind k)
    {
        switch (k)
        {
            case MidiTrigger::Kind::note:          return "note";
            case MidiTrigger::Kind::programChange: return "pc";
            case MidiTrigger::Kind::controlChange: break;
        }
        return "cc";
    }
}

std::vector<MidiBinding> makeDefaultMidiBindings()
{
    using K = MidiTrigger::Kind;
    return {
        { MidiAction::nextCue,       1, { K::controlChange, 0, 64 } },   // sustain pedal
        { MidiAction::programRecall, 1, { K::programChange, 0, 0 } },
        { MidiAction::expression,    1, { K::controlChange, 0, 11 } },   // expression
        { MidiAction::expression,    2, { K::controlChange, 0, 1 } },    // mod wheel
    };
}

juce::String describe (MidiAction action, int index)
{
    for (auto& a : actionNames)
        if (a.action == action)
            return (action == MidiAction::stomp || action == MidiAction::expression)
                       ? juce::String (a.label) + " " + juce::String (index)
                       : juce::String (a.label);

    return "?";
}

juce::String describe (const MidiTrigger& t)
{
    juce::String text;

    switch (t.kind)
    {
        case MidiTrigger::Kind::controlChange: text << "CC " << t.number; break;
        case MidiTrigger::Kind::note:          text << "Note " << juce::MidiMessage::getMidiNoteName (t.number, true, true, 3); break;
        case MidiTrigger::Kind::programChange: text << "Program change"; break;
    }

    text << (t.channel == 0 ? ", any channel" : ", channel " + juce::String (t.channel));
    return text;
}

juce::String describe (const MidiOutMessage& m)
{
    return m.kind == MidiOutMessage::Kind::programChange
               ? "PC " + juce::String (m.channel) + " " + juce::String (m.number)
               : "CC " + juce::String (m.channel) + " " + juce::String (m.number) + " " + juce::String (m.value);
}

juce::String formatMidiOut (const std::vector<MidiOutMessage>& messages)
{
    juce::StringArray lines;

    for (auto& m : messages)
        lines.add (describe (m));

    return lines.joinIntoString ("\n");
}

bool parseMidiOut (const juce::String& text, std::vector<MidiOutMessage>& out, juce::String& error)
{
    out.clear();
    error.clear();

    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();

        if (line.isEmpty())
            continue;

        const auto tokens = juce::StringArray::fromTokens (line.toUpperCase(), " ,", {});
        const auto valid = [] (const juce::String& s, int low, int high)
        {
            return s.containsOnly ("0123456789") && s.isNotEmpty() && s.getIntValue() >= low && s.getIntValue() <= high;
        };

        MidiOutMessage m;

        if (tokens.size() == 3 && tokens[0] == "PC" && valid (tokens[1], 1, 16) && valid (tokens[2], 0, 127))
        {
            m.kind = MidiOutMessage::Kind::programChange;
            m.channel = tokens[1].getIntValue();
            m.number = tokens[2].getIntValue();
        }
        else if (tokens.size() == 4 && tokens[0] == "CC" && valid (tokens[1], 1, 16)
                 && valid (tokens[2], 0, 127) && valid (tokens[3], 0, 127))
        {
            m.kind = MidiOutMessage::Kind::controlChange;
            m.channel = tokens[1].getIntValue();
            m.number = tokens[2].getIntValue();
            m.value = tokens[3].getIntValue();
        }
        else
        {
            error = "Can't read \"" + line + "\". Use \"PC channel program\" or \"CC channel number value\".";
            return false;
        }

        out.push_back (m);
    }

    return true;
}

juce::var bindingsToVar (const std::vector<MidiBinding>& bindings)
{
    juce::Array<juce::var> list;

    for (auto& b : bindings)
    {
        auto* o = new juce::DynamicObject();

        for (auto& a : actionNames)
            if (a.action == b.action)
                o->setProperty ("action", a.id);

        o->setProperty ("index", b.index);
        o->setProperty ("kind", kindId (b.trigger.kind));
        o->setProperty ("channel", b.trigger.channel);
        o->setProperty ("number", b.trigger.number);
        list.add (juce::var (o));
    }

    return list;
}

std::vector<MidiBinding> bindingsFromVar (const juce::var& v)
{
    std::vector<MidiBinding> bindings;

    if (auto* list = v.getArray())
    {
        for (auto& item : *list)
        {
            MidiBinding b;
            const auto actionId = item.getProperty ("action", {}).toString();
            bool known = false;

            for (auto& a : actionNames)
                if (actionId == a.id)
                {
                    b.action = a.action;
                    known = true;
                }

            if (! known)
                continue;   // from a newer Encore: skip rather than misread

            b.index = juce::jlimit (1, maxStomps, (int) item.getProperty ("index", 1));
            const auto kind = item.getProperty ("kind", "cc").toString();
            b.trigger.kind = kind == "note" ? MidiTrigger::Kind::note
                           : kind == "pc"   ? MidiTrigger::Kind::programChange
                                            : MidiTrigger::Kind::controlChange;
            b.trigger.channel = juce::jlimit (0, 16, (int) item.getProperty ("channel", 0));
            b.trigger.number = juce::jlimit (0, 127, (int) item.getProperty ("number", 0));
            bindings.push_back (b);
        }
    }

    return bindings;
}

} // namespace encore
