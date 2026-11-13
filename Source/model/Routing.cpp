// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/Routing.h"

#include <algorithm>
#include <cmath>

namespace encore
{

namespace routing
{
    float gainFor (float dB) noexcept
    {
        if (! std::isfinite (dB) || dB <= silentDb)
            return 0.0f;

        return std::pow (10.0f, juce::jmin (dB, maxLevelDb) * 0.05f);
    }

    float clampLevel (float dB) noexcept
    {
        if (! std::isfinite (dB))
            return dB > 0.0f ? maxLevelDb : silentDb;

        return juce::jlimit (silentDb, maxLevelDb, dB);
    }

    juce::String cleanName (const juce::String& name)
    {
        // Line breaks and tabs would break rows and menus.
        return name.replaceCharacters ("\r\n\t", "   ").trim().substring (0, maxNameLength).trim();
    }

    bool sameName (const juce::String& a, const juce::String& b) noexcept
    {
        return a.trim().equalsIgnoreCase (b.trim());
    }
}

namespace
{
    float finiteOr (float value, float fallback) noexcept
    {
        return std::isfinite (value) ? value : fallback;
    }

    template <typename List>
    juce::String uniqueName (const List& list, const juce::String& base)
    {
        auto taken = [&] (const juce::String& name)
        {
            for (auto& item : list)
                if (routing::sameName (item.name, name))
                    return true;

            return false;
        };

        auto clean = routing::cleanName (base);

        if (clean.isEmpty())
            clean = "Untitled";

        if (! taken (clean))
            return clean;

        for (int n = 2;; ++n)
        {
            const auto suffix = " " + juce::String (n);
            const auto candidate = clean.substring (0, routing::maxNameLength - suffix.length()).trim() + suffix;

            if (! taken (candidate))
                return candidate;
        }
    }

    template <typename List>
    int indexOf (const List& list, const juce::String& name)
    {
        if (name.trim().isEmpty())
            return -1;

        for (size_t i = 0; i < list.size(); ++i)
            if (routing::sameName (list[i].name, name))
                return (int) i;

        return -1;
    }
}

//==============================================================================
bool OutputEq::isFlat() const noexcept
{
    return lowCut <= minLowCut && highCut >= maxHighCut
           && juce::exactlyEqual (bass, 0.0f) && juce::exactlyEqual (mid, 0.0f) && juce::exactlyEqual (treble, 0.0f);
}

OutputEq OutputEq::clamped() const noexcept
{
    const OutputEq defaults;
    OutputEq e;
    e.lowCut  = juce::jlimit (minLowCut, maxLowCut, finiteOr (lowCut, defaults.lowCut));
    e.highCut = juce::jlimit (minHighCut, maxHighCut, finiteOr (highCut, defaults.highCut));
    e.bass    = juce::jlimit (-maxGainDb, maxGainDb, finiteOr (bass, 0.0f));
    e.mid     = juce::jlimit (-maxGainDb, maxGainDb, finiteOr (mid, 0.0f));
    e.midFreq = juce::jlimit (minMidFreq, maxMidFreq, finiteOr (midFreq, defaults.midFreq));
    e.treble  = juce::jlimit (-maxGainDb, maxGainDb, finiteOr (treble, 0.0f));
    return e;
}

//==============================================================================
const RoutingInput* RoutingConfig::findInput (const juce::String& name) const
{
    const auto i = indexOfInput (name);
    return i >= 0 ? &inputs[(size_t) i] : nullptr;
}

const RoutingOutput* RoutingConfig::findOutput (const juce::String& name) const
{
    const auto i = indexOfOutput (name);
    return i >= 0 ? &outputs[(size_t) i] : nullptr;
}

RoutingOutput* RoutingConfig::findOutput (const juce::String& name)
{
    const auto i = indexOfOutput (name);
    return i >= 0 ? &outputs[(size_t) i] : nullptr;
}

int RoutingConfig::indexOfInput (const juce::String& name) const   { return indexOf (inputs, name); }
int RoutingConfig::indexOfOutput (const juce::String& name) const  { return indexOf (outputs, name); }

const RoutingInput& RoutingConfig::inputFor (const juce::String& name) const
{
    if (const auto* input = findInput (name))
        return *input;

    static const RoutingInput fallback { routing::defaultInputName, 1 };
    return inputs.empty() ? fallback : inputs.front();
}

juce::String RoutingConfig::uniqueInputName (const juce::String& base) const   { return uniqueName (inputs, base); }
juce::String RoutingConfig::uniqueOutputName (const juce::String& base) const  { return uniqueName (outputs, base); }

RoutingConfig RoutingConfig::sanitised() const
{
    RoutingConfig clean;

    for (const auto& in : inputs)
    {
        if ((int) clean.inputs.size() >= routing::maxInputs)
            break;

        RoutingInput i;
        i.name = clean.uniqueInputName (in.name.trim().isEmpty() ? juce::String ("Input") : in.name);
        i.channel = juce::jlimit (1, routing::maxChannel, in.channel);
        clean.inputs.push_back (i);
    }

    for (const auto& out : outputs)
    {
        if ((int) clean.outputs.size() >= routing::maxOutputs)
            break;

        RoutingOutput o = out;
        o.name = clean.uniqueOutputName (out.name.trim().isEmpty() ? juce::String ("Output") : out.name);
        o.channels.clear();

        // 1 or 2 channels, each in range, the pair not doubled; "all" stays empty.
        for (auto ch : out.channels)
            if (o.channels.size() < 2 && ch >= 1 && ch <= routing::maxChannel
                && std::find (o.channels.begin(), o.channels.end(), ch) == o.channels.end())
                o.channels.push_back (ch);

        if (o.channels.empty() && ! out.channels.empty())
            o.channels.push_back (1);   // had channels, all invalid: mono on 1, never silently "all"

        o.levelDb = routing::clampLevel (out.levelDb);
        o.eq = out.eq.clamped();
        o.delayMs = juce::jlimit (0.0f, routing::maxDelayMs, finiteOr (out.delayMs, 0.0f));
        o.delayMs = std::round (o.delayMs / routing::delayStepMs) * routing::delayStepMs;
        clean.outputs.push_back (std::move (o));
    }

    const auto defaults = makeDefaultRouting();

    if (clean.inputs.empty())
        clean.inputs = defaults.inputs;

    if (clean.outputs.empty())
        clean.outputs = defaults.outputs;

    return clean;
}

RoutingConfig makeDefaultRouting (int guitarChannel)
{
    RoutingConfig config;
    config.inputs.push_back ({ routing::defaultInputName, juce::jlimit (1, routing::maxChannel, guitarChannel) });

    RoutingOutput main;
    main.name = routing::defaultOutputName;
    config.outputs.push_back (main);
    return config;
}

//==============================================================================
juce::String describeChannels (const RoutingOutput& output)
{
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    if (output.isAllOutputs())
        return "all outputs";

    if (output.channels.size() == 2)
        return juce::String (output.channels[0]) + "/" + juce::String (output.channels[1]) + dot + "stereo";

    return juce::String (output.channels[0]) + dot + "mono";
}

juce::String describeShortChannels (const RoutingOutput& output)
{
    if (output.isAllOutputs())
        return "all";

    juce::StringArray parts;

    for (auto ch : output.channels)
        parts.add (juce::String (ch));

    return parts.joinIntoString ("/");
}

juce::String describeLevel (float dB, bool on)
{
    if (! on || dB <= routing::silentDb)
        return "Off";

    // A real minus sign, as the boards write levels; 0.0 never gets one.
    const auto rounded = std::round (dB * 10.0f) / 10.0f;
    const auto text = juce::String (std::abs (rounded), 1) + " dB";
    return rounded < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + text : text;
}

juce::String describeDelay (float ms)
{
    return juce::String (ms, 1) + " ms";
}

juce::String describeDistance (float ms)
{
    return juce::String (juce::CharPointer_UTF8 ("\xe2\x89\x88 ")) + juce::String (ms * 0.001f * routing::speedOfSound, 2) + " m";
}

//==============================================================================
namespace
{
    juce::var eqToVar (const OutputEq& eq)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("lowCut", eq.lowCut);
        o->setProperty ("highCut", eq.highCut);
        o->setProperty ("bass", eq.bass);
        o->setProperty ("mid", eq.mid);
        o->setProperty ("midFreq", eq.midFreq);
        o->setProperty ("treble", eq.treble);
        return juce::var (o);
    }

    float number (const juce::var& o, const char* key, float fallback)
    {
        const auto v = o.getProperty (key, {});
        return (v.isDouble() || v.isInt() || v.isInt64()) ? (float) (double) v : fallback;
    }

    OutputEq eqFromVar (const juce::var& v)
    {
        OutputEq eq;

        if (v.isObject())
        {
            eq.lowCut  = number (v, "lowCut", eq.lowCut);
            eq.highCut = number (v, "highCut", eq.highCut);
            eq.bass    = number (v, "bass", eq.bass);
            eq.mid     = number (v, "mid", eq.mid);
            eq.midFreq = number (v, "midFreq", eq.midFreq);
            eq.treble  = number (v, "treble", eq.treble);
        }

        return eq;
    }
}

juce::var routingToVar (const RoutingConfig& config)
{
    juce::Array<juce::var> inputs, outputs;

    for (const auto& in : config.inputs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", in.name);
        o->setProperty ("channel", in.channel);
        inputs.add (juce::var (o));
    }

    for (const auto& out : config.outputs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", out.name);

        if (out.isAllOutputs())
        {
            o->setProperty ("channels", "all");
        }
        else
        {
            juce::Array<juce::var> channels;
            for (auto ch : out.channels)
                channels.add (ch);

            o->setProperty ("channels", channels);
        }

        // -inf has no JSON number: "off".
        if (out.levelDb <= routing::silentDb)
            o->setProperty ("level", "off");
        else
            o->setProperty ("level", out.levelDb);

        o->setProperty ("mute", out.mute);
        o->setProperty ("eq", eqToVar (out.eq));
        o->setProperty ("delayMs", out.delayMs);
        outputs.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("inputs", inputs);
    root->setProperty ("outputs", outputs);
    return juce::var (root);
}

RoutingConfig routingFromVar (const juce::var& v, int guitarChannel)
{
    if (! v.isObject())
        return makeDefaultRouting (guitarChannel);

    RoutingConfig config;

    if (auto* inputs = v.getProperty ("inputs", {}).getArray())
        for (auto& item : *inputs)
            if (item.isObject())
                config.inputs.push_back ({ item.getProperty ("name", {}).toString(),
                                           (int) number (item, "channel", 1.0f) });

    if (auto* outputs = v.getProperty ("outputs", {}).getArray())
    {
        for (auto& item : *outputs)
        {
            if (! item.isObject())
                continue;

            RoutingOutput out;
            out.name = item.getProperty ("name", {}).toString();

            const auto channels = item.getProperty ("channels", {});

            if (auto* list = channels.getArray())
            {
                for (auto& ch : *list)
                    out.channels.push_back ((ch.isInt() || ch.isInt64() || ch.isDouble()) ? (int) ch : 0);

                if (out.channels.empty())
                    out.channels.push_back (1);   // "[]" isn't "all": only the word is
            }
            else if (channels.toString() != "all")
            {
                out.channels.push_back (1);
            }

            const auto level = item.getProperty ("level", {});
            out.levelDb = level.toString() == "off" ? routing::silentDb : number (item, "level", 0.0f);
            out.mute = (bool) item.getProperty ("mute", false);
            out.eq = eqFromVar (item.getProperty ("eq", {}));
            out.delayMs = number (item, "delayMs", 0.0f);
            config.outputs.push_back (std::move (out));
        }
    }

    // Keep what was readable; a list that came out empty gets the defaults.
    if (config.inputs.empty())
        config.inputs = makeDefaultRouting (guitarChannel).inputs;

    return config.sanitised();
}

} // namespace encore
