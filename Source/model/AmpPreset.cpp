// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/AmpPreset.h"

#include <cmath>

namespace encore
{

namespace
{
    juce::var object() { return juce::var (new juce::DynamicObject()); }

    juce::var get (const juce::var& o, const char* key)       { return o.getProperty (juce::Identifier (key), {}); }

    juce::String str (const juce::var& o, const char* key, const juce::String& fallback = {})
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : v.toString();
    }

    int integer (const juce::var& o, const char* key, int fallback)
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : (int) v;
    }

    float number (const juce::var& o, const char* key, float fallback)
    {
        const auto v = get (o, key);
        return v.isVoid() ? fallback : (float) (double) v;
    }

    /** The keys of `o` not in `known`, as an object (void when there are none). */
    juce::var unknownKeys (const juce::var& o, std::initializer_list<const char*> known)
    {
        auto* from = o.getDynamicObject();

        if (from == nullptr)
            return {};

        juce::var rest;

        for (const auto& [key, value] : from->getProperties())
        {
            if (std::any_of (known.begin(), known.end(), [&key] (const char* k) { return key.toString() == k; }))
                continue;

            if (rest.isVoid())
                rest = object();

            rest.getDynamicObject()->setProperty (key, value);
        }

        return rest;
    }

    /** A new object starting from the kept unknown keys. */
    juce::var startFrom (const juce::var& extra)
    {
        auto o = object();

        if (auto* kept = extra.getDynamicObject())
            for (const auto& [key, value] : kept->getProperties())
                o.getDynamicObject()->setProperty (key, value);

        return o;
    }

    void set (juce::var& o, const char* key, const juce::var& value) { o.getDynamicObject()->setProperty (juce::Identifier (key), value); }

    void setIf (juce::var& o, const char* key, const juce::String& value)
    {
        if (value.isNotEmpty())
            set (o, key, value);
    }

    bool sameExtra (const juce::var& a, const juce::var& b)
    {
        return juce::JSON::toString (a, true) == juce::JSON::toString (b, true);
    }

    const std::initializer_list<const char*> lookKeys { "body", "tolex", "piping", "panel", "grille", "corners", "studs" };
    const std::initializer_list<const char*> partKeys { "id", "type", "x", "y", "w", "h", "style", "size", "drives", "label", "labelPos",
                                                        "scale", "switch", "top", "colour", "lights", "value", "mat", "text", "font",
                                                        "icon", "shape", "logoSize" };
    const std::initializer_list<const char*> presetKeys { "format", "id", "name", "version", "look", "parts", "capture" };

    juce::var partToVar (const AmpPart& p)
    {
        auto o = startFrom (p.extra);
        set (o, "id", p.id);
        set (o, "type", p.type);
        set (o, "x", p.x);
        set (o, "y", p.y);
        set (o, "w", p.w);
        set (o, "h", p.h);
        setIf (o, "style", p.style);
        setIf (o, "size", p.size);
        setIf (o, "label", p.label);
        setIf (o, "labelPos", p.labelPos);
        setIf (o, "scale", p.scale);
        setIf (o, "switch", p.switchStyle);
        setIf (o, "top", p.top);
        setIf (o, "colour", p.colour);
        setIf (o, "lights", p.lights);
        setIf (o, "mat", p.mat);
        setIf (o, "text", p.text);
        setIf (o, "font", p.font);
        setIf (o, "icon", p.icon);
        setIf (o, "shape", p.shape);

        if (p.isKnob() || p.isToggle())
        {
            set (o, "drives", p.drives);   // "" = not set, written as such
            set (o, "value", p.value);
        }

        if (p.logoSize > 0)
            set (o, "logoSize", p.logoSize);

        return o;
    }

    AmpPart partFromVar (const juce::var& o)
    {
        AmpPart p;
        p.id = str (o, "id");
        p.type = str (o, "type");
        p.x = integer (o, "x", 0);
        p.y = integer (o, "y", 0);
        p.w = juce::jmax (0, integer (o, "w", 0));
        p.h = juce::jmax (0, integer (o, "h", 0));
        p.style = str (o, "style");
        p.size = str (o, "size");
        p.drives = str (o, "drives");
        p.label = str (o, "label");
        p.labelPos = str (o, "labelPos");
        p.scale = str (o, "scale");
        p.switchStyle = str (o, "switch");
        p.top = str (o, "top");
        p.colour = str (o, "colour");
        p.lights = str (o, "lights");
        p.value = juce::jlimit (0.0f, 1.0f, number (o, "value", 0.5f));
        p.mat = str (o, "mat");
        p.text = str (o, "text");
        p.font = str (o, "font");
        p.icon = str (o, "icon");
        p.shape = str (o, "shape");
        p.logoSize = juce::jmax (0, integer (o, "logoSize", 0));
        p.extra = unknownKeys (o, partKeys);

        if (! std::isfinite (p.value))
            p.value = 0.5f;

        return p;
    }
}

//==============================================================================
bool AmpLook::operator== (const AmpLook& o) const
{
    return body == o.body && tolex == o.tolex && piping == o.piping && panel == o.panel && grille == o.grille
        && corners == o.corners && studs == o.studs && sameExtra (extra, o.extra);
}

bool AmpPart::operator== (const AmpPart& o) const
{
    return id == o.id && type == o.type && x == o.x && y == o.y && w == o.w && h == o.h && style == o.style && size == o.size
        && drives == o.drives && label == o.label && labelPos == o.labelPos && scale == o.scale && switchStyle == o.switchStyle
        && top == o.top && colour == o.colour && lights == o.lights && std::abs (value - o.value) < 1.0e-6f && mat == o.mat
        && text == o.text && font == o.font && icon == o.icon && shape == o.shape && logoSize == o.logoSize && sameExtra (extra, o.extra);
}

bool AmpPart::drivesParameter() const noexcept
{
    return (isKnob() || isToggle()) && ampPreset::parameterFor (drives).isNotEmpty();
}

int AmpPart::width() const noexcept   { return isKnob() ? ampPreset::knobSize (size) : w; }
int AmpPart::height() const noexcept  { return isKnob() ? ampPreset::knobSize (size) : h; }

juce::String AmpPart::fixedTooltip() const
{
    if (isKnob())
        return "Fixed at " + ampPreset::tenth (value) + " in this capture";

    const auto up = value >= 0.5f;
    const auto push = switchStyle == "push";
    return juce::String ("Fixed ") + (push ? (up ? "out" : "pressed in") : (up ? "up" : "down")) + " in this capture";
}

bool AmpPreset::operator== (const AmpPreset& o) const
{
    return id == o.id && name == o.name && version == o.version && look == o.look && parts == o.parts && capture == o.capture
        && sameExtra (extra, o.extra);
}

const AmpPart* AmpPreset::findPart (const juce::String& partId) const
{
    for (const auto& p : parts)
        if (p.id == partId)
            return &p;

    return nullptr;
}

bool AmpPreset::canSave (juce::String* why) const
{
    int notSet = 0;

    for (const auto& p : parts)
        if (p.isNotSet())
            ++notSet;

    if (notSet > 0 && why != nullptr)
        *why = notSet == 1 ? juce::String ("A knob is NOT SET: pick what it drives, or Fixed")
                           : juce::String (notSet) + " knobs are NOT SET: pick what each drives, or Fixed";

    return notSet == 0;
}

//==============================================================================
namespace ampPreset
{

int knobSize (const juce::String& letter) noexcept
{
    if (letter == "S")  return 26;
    if (letter == "L")  return 48;
    if (letter == "XL") return 64;
    return 34;
}

juce::String parameterFor (const juce::String& drives)
{
    if (drives == "input" || drives == "bass" || drives == "mid" || drives == "treble" || drives == "output")
        return drives;

    if (drives == "levelMatch")
        return "normalise";

    return {};
}

juce::String tenth (float value)
{
    const auto n = std::round (value * 100.0f) / 10.0f;
    return std::abs (n - std::round (n)) < 1.0e-4f ? juce::String ((int) std::round (n)) : juce::String (n, 1);
}

juce::var toVar (const AmpPreset& preset)
{
    auto root = startFrom (preset.extra);
    set (root, "id", preset.id);
    set (root, "name", preset.name);
    set (root, "version", preset.version);

    auto look = startFrom (preset.look.extra);
    set (look, "body", preset.look.body);
    set (look, "tolex", preset.look.tolex);
    set (look, "piping", preset.look.piping);
    set (look, "panel", preset.look.panel);
    set (look, "grille", preset.look.grille);
    set (look, "corners", preset.look.corners);
    set (look, "studs", preset.look.studs);
    set (root, "look", look);

    juce::Array<juce::var> parts;

    for (const auto& p : preset.parts)
        parts.add (partToVar (p));

    set (root, "parts", parts);
    set (root, "capture", preset.capture);
    return root;
}

std::optional<AmpPreset> fromVar (const juce::var& root, juce::String& error)
{
    error.clear();

    if (! root.isObject())
    {
        error = "An amp in the show is damaged";
        return std::nullopt;
    }

    AmpPreset preset;
    preset.id = str (root, "id");
    preset.name = str (root, "name", "Amp");
    preset.version = integer (root, "version", AmpPreset::currentVersion);
    preset.capture = str (root, "capture");
    preset.extra = unknownKeys (root, presetKeys);

    if (preset.id.isEmpty())
    {
        error = "An amp without an id";
        return std::nullopt;
    }

    if (const auto look = get (root, "look"); look.isObject())
    {
        preset.look.body = str (look, "body", preset.look.body);
        preset.look.tolex = str (look, "tolex", preset.look.tolex);
        preset.look.piping = str (look, "piping", preset.look.piping);
        preset.look.panel = str (look, "panel", preset.look.panel);
        preset.look.grille = str (look, "grille", preset.look.grille);
        preset.look.corners = str (look, "corners", preset.look.corners);
        preset.look.studs = (bool) get (look, "studs");
        preset.look.extra = unknownKeys (look, lookKeys);
    }

    if (auto* parts = get (root, "parts").getArray())
        for (const auto& p : *parts)
            if (p.isObject())
                preset.parts.push_back (partFromVar (p));

    return preset;
}

juce::String toJson (const AmpPreset& preset)
{
    auto root = toVar (preset);
    root.getDynamicObject()->setProperty ("format", "encore-amp");
    return juce::JSON::toString (root, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine));
}

std::optional<AmpPreset> fromJson (const juce::String& text, juce::String& error)
{
    juce::var root;
    const auto result = juce::JSON::parse (text, root);

    if (result.failed() || ! root.isObject())
    {
        error = "The amp file is damaged (" + result.getErrorMessage() + ")";
        return std::nullopt;
    }

    if (str (root, "format") != "encore-amp")
    {
        error = "This isn't an Encore amp file";
        return std::nullopt;
    }

    return fromVar (root, error);
}

} // namespace ampPreset

} // namespace encore
