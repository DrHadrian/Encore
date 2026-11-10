// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/core/Parameter.h"

namespace encore
{

Parameter::Parameter (Spec specToUse)
    : spec (std::move (specToUse)),
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
