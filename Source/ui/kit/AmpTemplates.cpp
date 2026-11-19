// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/AmpTemplates.h"

#include "EncoreAmpArt.h"

namespace encore::kit::ampTemplates
{

namespace
{
    struct Data
    {
        std::vector<Template> templates;
        std::vector<Example> examples;
        AmpPreset fallback;

        Data()
        {
            const auto text = juce::String::fromUTF8 (EncoreAmpArt::amptemplates_json, EncoreAmpArt::amptemplates_jsonSize);
            const auto root = juce::JSON::parse (text);

            if (auto* list = root.getProperty ("templates", {}).getArray())
            {
                for (const auto& t : *list)
                {
                    const auto key = t.getProperty ("key", {}).toString();
                    auto amp = t.clone();
                    auto* o = amp.getDynamicObject();
                    o->removeProperty ("key");
                    o->setProperty ("id", "template-" + key);
                    juce::String error;

                    if (auto preset = ampPreset::fromVar (amp, error))
                        templates.push_back ({ key, preset->name, std::move (*preset) });
                }
            }

            if (auto* list = root.getProperty ("examples", {}).getArray())
            {
                for (const auto& e : *list)
                {
                    const auto name = e.getProperty ("name", {}).toString();
                    const auto key = e.getProperty ("template", {}).toString();

                    for (const auto& t : templates)
                    {
                        if (t.key == key)
                        {
                            auto amp = t.amp;
                            amp.id = "example-" + name.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 -").replaceCharacter (' ', '-');
                            amp.name = name;
                            examples.push_back ({ std::move (amp), key });
                        }
                    }
                }
            }

            if (! examples.empty())
                fallback = examples.front().amp;
        }
    };

    const Data& data()
    {
        static const Data d;   // read once, on first use
        return d;
    }
}

const std::vector<Template>& all()          { return data().templates; }
const std::vector<Example>& examples()      { return data().examples; }
const AmpPreset& defaultFace()              { return data().fallback; }

const Template* find (const juce::String& key)
{
    for (const auto& t : data().templates)
        if (t.key == key)
            return &t;

    return nullptr;
}

AmpPreset makeFrom (const juce::String& key, const juce::String& name)
{
    AmpPreset amp;

    if (const auto* t = find (key))
        amp = t->amp;

    amp.id = juce::Uuid().toString();
    amp.name = name;
    amp.capture = {};
    return amp;
}

} // namespace encore::kit::ampTemplates
