// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "model/StageLayout.h"

namespace encore
{

namespace
{
    struct TypeInfo
    {
        StageWidgetType type;
        const char* id;
        const char* name;
    };

    constexpr TypeInfo types[] = {
        { StageWidgetType::currentCue,     "currentCue",     "Current cue" },
        { StageWidgetType::nextCue,        "nextCue",        "Next cue" },
        { StageWidgetType::patchName,      "patch",          "Patch" },
        { StageWidgetType::cueNote,        "cueNote",        "Cue note and bar" },
        { StageWidgetType::setList,        "setList",        "Set list" },
        { StageWidgetType::stomps,         "stomps",         "Stomp switches" },
        { StageWidgetType::pedals,         "pedals",         "Expression pedals" },
        { StageWidgetType::tuner,          "tuner",          "Tuner" },
        { StageWidgetType::meters,         "meters",         "Level meters" },
        { StageWidgetType::clock,          "clock",          "Clock" },
        { StageWidgetType::health,         "health",         "System health" },
        { StageWidgetType::panicButton,    "panicButton",    "Panic button" },
        { StageWidgetType::lockButton,     "lockButton",     "Lock button" },
        { StageWidgetType::nextButton,     "nextButton",     "Next button" },
        { StageWidgetType::previousButton, "previousButton", "Previous button" },
        { StageWidgetType::text,           "text",           "Text" },
    };

    const TypeInfo& info (StageWidgetType t)
    {
        for (auto& i : types)
            if (i.type == t)
                return i;

        return types[std::size (types) - 1];
    }

    StageWidget widget (StageWidgetType type, int x, int y, int w, int h, float scale = 1.0f)
    {
        return { type, x, y, w, h, scale, {} };
    }
}

juce::String toString (StageWidgetType t)  { return info (t).id; }
juce::String describe (StageWidgetType t)  { return info (t).name; }

StageWidgetType stageWidgetTypeFromString (const juce::String& s, bool& known)
{
    for (auto& i : types)
    {
        if (s == i.id)
        {
            known = true;
            return i.type;
        }
    }

    known = false;
    return StageWidgetType::text;
}

std::vector<StageWidgetType> allStageWidgetTypes()
{
    std::vector<StageWidgetType> result;

    for (auto& i : types)
        result.push_back (i.type);

    return result;
}

StageWidget clampToGrid (StageWidget w)
{
    w.width = juce::jlimit (1, stageGridColumns, w.width);
    w.height = juce::jlimit (1, stageGridRows, w.height);
    w.x = juce::jlimit (0, stageGridColumns - w.width, w.x);
    w.y = juce::jlimit (0, stageGridRows - w.height, w.y);
    w.textScale = juce::jlimit (0.5f, 3.0f, w.textScale);
    return w;
}

StageLayout makeDefaultStageLayout()
{
    StageLayout layout;
    layout.id = "default";
    layout.name = "Default";
    layout.widgets = {
        widget (StageWidgetType::currentCue,  0,  0, 16, 5, 1.2f),
        widget (StageWidgetType::clock,      16,  0,  4, 2),
        widget (StageWidgetType::health,     20,  0,  4, 2),
        widget (StageWidgetType::lockButton, 16,  2,  4, 3),
        widget (StageWidgetType::panicButton,20,  2,  4, 3),
        widget (StageWidgetType::patchName,   0,  5, 16, 2),
        widget (StageWidgetType::cueNote,     0,  7, 16, 2),
        widget (StageWidgetType::nextCue,     0,  9, 16, 3),
        widget (StageWidgetType::setList,    16,  5,  8, 11),
        widget (StageWidgetType::stomps,      0, 12, 10, 4),
        widget (StageWidgetType::pedals,     10, 12,  3, 4),
        widget (StageWidgetType::meters,     13, 12,  3, 4),
    };
    return layout;
}

const StageLayout* StageSettings::findLayout (const juce::String& id) const
{
    for (auto* wanted : { &id, &defaultLayoutId })
        for (auto& layout : layouts)
            if (wanted->isNotEmpty() && layout.id == *wanted)
                return &layout;

    return layouts.empty() ? nullptr : &layouts.front();
}

//==============================================================================
juce::var stageToVar (const StageSettings& stage)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("tunerMutes", stage.tunerMutes);
    root->setProperty ("referencePitch", stage.referencePitch);
    root->setProperty ("panic", stage.panic == PanicBehaviour::latch ? "latch" : "recover");

    if (stage.defaultLayoutId.isNotEmpty())
        root->setProperty ("defaultLayout", stage.defaultLayoutId);

    juce::Array<juce::var> layouts;

    for (auto& layout : stage.layouts)
    {
        auto* l = new juce::DynamicObject();
        l->setProperty ("id", layout.id);
        l->setProperty ("name", layout.name);

        juce::Array<juce::var> widgets;

        for (auto& w : layout.widgets)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type", toString (w.type));
            o->setProperty ("x", w.x);
            o->setProperty ("y", w.y);
            o->setProperty ("w", w.width);
            o->setProperty ("h", w.height);

            if (std::abs (w.textScale - 1.0f) > 0.001f)
                o->setProperty ("scale", w.textScale);

            if (w.text.isNotEmpty())
                o->setProperty ("text", w.text);

            widgets.add (juce::var (o));
        }

        l->setProperty ("widgets", widgets);
        layouts.add (juce::var (l));
    }

    root->setProperty ("layouts", layouts);
    return juce::var (root);
}

StageSettings stageFromVar (const juce::var& v)
{
    StageSettings stage;

    if (! v.isObject())
        return stage;

    const auto prop = [] (const juce::var& o, const char* key, const juce::var& fallback)
    {
        const auto value = o.getProperty (key, {});
        return value.isVoid() ? fallback : value;
    };

    stage.tunerMutes = (bool) prop (v, "tunerMutes", true);
    stage.referencePitch = juce::jlimit (415.0f, 466.0f, (float) (double) prop (v, "referencePitch", 440.0));
    stage.panic = prop (v, "panic", "recover").toString() == "latch" ? PanicBehaviour::latch : PanicBehaviour::recover;
    stage.defaultLayoutId = prop (v, "defaultLayout", juce::String()).toString();

    if (auto* layouts = v.getProperty ("layouts", {}).getArray())
    {
        for (auto& l : *layouts)
        {
            StageLayout layout;
            layout.id = prop (l, "id", juce::Uuid().toString()).toString();
            layout.name = prop (l, "name", "Layout").toString();

            if (auto* widgets = l.getProperty ("widgets", {}).getArray())
            {
                for (auto& o : *widgets)
                {
                    bool known = false;
                    StageWidget w;
                    w.type = stageWidgetTypeFromString (prop (o, "type", "").toString(), known);

                    if (! known)
                        continue;   // a widget from a newer Encore: skip it

                    w.x = (int) prop (o, "x", 0);
                    w.y = (int) prop (o, "y", 0);
                    w.width = (int) prop (o, "w", 4);
                    w.height = (int) prop (o, "h", 2);
                    w.textScale = (float) (double) prop (o, "scale", 1.0);
                    w.text = prop (o, "text", juce::String()).toString();
                    layout.widgets.push_back (clampToGrid (w));
                }
            }

            stage.layouts.push_back (std::move (layout));
        }
    }

    return stage;
}

} // namespace encore
