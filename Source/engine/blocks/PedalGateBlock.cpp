// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/PedalGateBlock.h"

namespace encore
{

namespace
{
    // Factory presets (ids stable once merged; values Proposed, GATE-PLAN §6).
    constexpr PresetValue cleanHiss[]  { { "threshold", 2.0f }, { "decay", 6.0f }, { "soft", (float) PedalGateBlock::Mode::soft } };
    constexpr PresetValue singleCoils[] { { "threshold", 3.0f }, { "decay", 5.0f } };
    constexpr PresetValue highGain[]   { { "threshold", 4.0f }, { "decay", 3.0f }, { "detect", (float) PedalGateBlock::Detect::guitar } };
    constexpr PresetValue djentTight[] { { "threshold", 5.0f }, { "decay", 1.0f }, { "detect", (float) PedalGateBlock::Detect::guitar } };

    constexpr FactoryPreset factoryPresets[] {
        { "cleanHiss",   "Clean / light hiss", cleanHiss },
        { "singleCoils", "Single coils",       singleCoils },
        { "highGain",    "High gain",          highGain },
        { "djentTight",  "Djent tight",        djentTight },
    };
}

PedalGateBlock::PedalGateBlock()
    : GateBlock (typeId, "Pedal Gate"),
      threshold (addParameter ({ "threshold", "Threshold", { 0.0f, 10.0f }, 3.0f, {}, 1 })),
      decay     (addParameter ({ "decay",     "Decay",     { 0.0f, 10.0f }, 4.0f, {}, 1 })),
      detect    (addParameter (Parameter::Spec::choice ("detect", "Detect", { "Input", "Guitar" }, (int) Detect::input))),
      soft      (addParameter (Parameter::Spec::choice ("soft", "Mode", { "Hard", "Soft" }, (int) Mode::hard)))
{
}

std::span<const FactoryPreset> PedalGateBlock::getFactoryPresets() const noexcept
{
    return factoryPresets;
}

GateSetup PedalGateBlock::setupFor (float thresholdKnob, float decayKnob, Detect detectSetting, Mode mode) noexcept
{
    GateSetup s;
    s.way = GateWay::smart;
    s.smartOffsetDb = offsetDbFor (thresholdKnob);
    s.releaseMs = releaseMsFor (decayKnob);
    s.smartExpander = mode == Mode::soft;
    s.rangeDb = mode == Mode::soft ? softRangeDb : hardRangeDb;
    s.ratio = softRatio;
    s.attackMs = attackMs;
    s.holdMs = holdMs;
    s.hysteresisDb = hysteresisDb;
    s.detector = dsp::DetectorMode::peak;
    s.detectorMs = detectorMs;
    s.key = detectSetting == Detect::guitar ? GateKey::guitar : GateKey::input;
    s.stereoLinked = true;
    return s;
}

GateSetup PedalGateBlock::readSetup (int numSamples) noexcept
{
    // The knobs ramp (30 ms); the switches are choices (the engine crossfades).
    const auto t = threshold.skip (numSamples);
    const auto d = decay.skip (numSamples);
    return setupFor (t, d, (Detect) detect.getChoiceIndex(), (Mode) soft.getChoiceIndex());
}

} // namespace encore
