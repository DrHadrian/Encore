// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "model/AmpPreset.h"

namespace encore::kit::ampTemplates
{

/** The amp builder's templates and example amps (design/AMP-BUILDER.md §6),
    built in (Resources/amps/amp-templates.json, read from the BAmpBuilder
    board): British gold, Black panel, Plexi, Cream, Tweed top, Grille head,
    Framed head, Jazz clean; and Encore Gold 30, JC-120, Super Crush 100,
    Blues Junior, 5150 III 50W. Names of real amps appear only as amp names
    and model-name text, never as logos. Templates carry no capture. */
struct Template
{
    juce::String key, name;   // "gold", "British gold"
    AmpPreset amp;            // its look and parts (id "template-<key>")
};

const std::vector<Template>& all();
const Template* find (const juce::String& key);

/** A new amp from a template: a fresh id, the given name, no capture. */
AmpPreset makeFrom (const juce::String& key, const juce::String& name);

/** The five example amps (ids "example-<name>", stable), each with its template's key. */
struct Example
{
    AmpPreset amp;
    juce::String templateKey;
};

const std::vector<Example>& examples();

/** The face an amp block shows when it has no amp of its own: Encore Gold 30
    (the Edit view's amp, rebuilt in the builder). Every control on it drives
    a real parameter. */
const AmpPreset& defaultFace();

} // namespace encore::kit::ampTemplates
