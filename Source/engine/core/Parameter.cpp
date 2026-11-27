// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Parameter.h"

namespace encore
{

Parameter::Spec Parameter::Spec::choice (juce::String id, juce::String name, juce::StringArray names, int defaultIndex)
{
    Spec spec;
    spec.id = std::move (id);
    spec.name = std::move (name);
    spec.defaultValue = (float) defaultIndex;
    spec.choices = std::move (names);
    return sanitised (std::move (spec));
}

Parameter::Spec Parameter::sanitised (Spec spec)
{
    if (spec.choices.isEmpty())
        return spec;

    // A choice is an index: whole steps from 0 to n-1, shown by name, no ramp.
    jassert (spec.choices.size() >= 2);   // one step is no choice
    spec.range = { 0.0f, (float) juce::jmax (1, spec.choices.size() - 1), 1.0f };
    spec.defaultValue = spec.range.snapToLegalValue (spec.defaultValue);
    spec.unit = {};
    spec.decimals = 0;
    spec.rampSeconds = 0.0;
    return spec;
}

Parameter::Parameter (Spec specToUse)
    : spec (sanitised (std::move (specToUse))),
      value (spec.range.snapToLegalValue (spec.defaultValue))
{
    smoother.setCurrentAndTargetValue (getValue());
}

void Parameter::setValue (float newValue) noexcept
{
    value.store (spec.range.snapToLegalValue (newValue), std::memory_order_relaxed);
}

juce::String Parameter::toText (float v) const
{
    if (spec.textFunction != nullptr)
        return spec.textFunction (v);

    if (isChoice())
        return spec.choices[juce::roundToInt (spec.range.snapToLegalValue (v))];

    // Note: juce::String (v, 0) means "full precision", not "no decimals".
    auto text = spec.decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, spec.decimals);
    return spec.unit.isEmpty() ? text : text + " " + spec.unit;
}

void Parameter::prepare (double sampleRate) noexcept
{
    smoother.reset (sampleRate, spec.rampSeconds);
    smoother.setCurrentAndTargetValue (getValue());
}

} // namespace encore
