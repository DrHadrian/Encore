// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/StudioGateBlock.h"

namespace encore
{

namespace
{
    juce::NormalisableRange<float> skewed (float start, float end, float centre)
    {
        juce::NormalisableRange<float> r { start, end };
        r.setSkewForCentre (centre);
        return r;
    }

    Parameter::Spec choice (const char* id, const char* name, juce::StringArray names, int defaultIndex)
    {
        return Parameter::Spec::choice (id, name, std::move (names), defaultIndex);
    }

    constexpr auto smart = (float) StudioGateBlock::Mode::smart;
    constexpr auto ducker = (float) StudioGateBlock::Mode::ducker;
    constexpr auto expander = (float) StudioGateBlock::Mode::expander;
    constexpr auto curveExpander = (float) StudioGateBlock::SmartCurve::expander;
    constexpr auto keyInput = (float) StudioGateBlock::Key::input;
    constexpr auto keyGuitar = (float) StudioGateBlock::Key::guitar;
    constexpr auto keyBlock = (float) StudioGateBlock::Key::block;

    // Factory presets (ids stable once merged; values Proposed, GATE-PLAN §6).
    // The S2's four in Smart mode, the same sound: Threshold (Smart) is 3 dB
    // per S2 step, Release the S2's Decay curve 20 * 40^(knob/10) ms, Soft the
    // expander 1:2 at -15 dB, Hard the gate at -90 dB, Detect the key, and the
    // S2's fixed values for the rest.
#define ENCORE_S2_FIXED { "attack", 0.3f }, { "hold", 20.0f }, { "hysteresis", 6.0f }, { "detectorTime", 10.0f }, { "ratio", 2.0f }
    constexpr PresetValue cleanHiss[]   { { "mode", smart }, { "smartOffset", 6.0f },  { "release", 182.92203f }, { "smartCurve", curveExpander },
                                          { "range", -15.0f }, { "key", keyInput }, ENCORE_S2_FIXED };
    constexpr PresetValue singleCoils[] { { "mode", smart }, { "smartOffset", 9.0f },  { "release", 126.49111f }, { "key", keyInput }, ENCORE_S2_FIXED };
    constexpr PresetValue highGain[]    { { "mode", smart }, { "smartOffset", 12.0f }, { "release", 60.485043f }, { "key", keyGuitar }, ENCORE_S2_FIXED };
    constexpr PresetValue djentTight[]  { { "mode", smart }, { "smartOffset", 15.0f }, { "release", 28.92251f },  { "key", keyGuitar }, ENCORE_S2_FIXED };
#undef ENCORE_S2_FIXED

    // Ducking: Key Block (which block is the player's pick in U2; until then
    // it keys from its own input and the info says so).
    constexpr PresetValue duckDelay[]  { { "mode", ducker }, { "key", keyBlock }, { "threshold", -40.0f }, { "range", -12.0f },
                                         { "attack", 5.0f }, { "release", 300.0f }, { "hold", 50.0f } };
    constexpr PresetValue duckReverb[] { { "mode", ducker }, { "key", keyBlock }, { "threshold", -40.0f }, { "range", -9.0f },
                                         { "attack", 5.0f }, { "release", 600.0f }, { "hold", 50.0f } };
    constexpr PresetValue softCleanup[]  { { "mode", expander }, { "ratio", 2.0f }, { "threshold", -55.0f }, { "range", -20.0f } };
    constexpr PresetValue swellCleanup[] { { "mode", expander }, { "ratio", 1.5f }, { "threshold", -60.0f }, { "range", -12.0f },
                                           { "release", 400.0f } };

    constexpr FactoryPreset factoryPresets[] {
        { "cleanHiss",    "Clean / light hiss",    cleanHiss },
        { "singleCoils",  "Single coils",          singleCoils },
        { "highGain",     "High gain",             highGain },
        { "djentTight",   "Djent tight",           djentTight },
        { "duckDelay",    "Ducking delay",         duckDelay },
        { "duckReverb",   "Ducking reverb",        duckReverb },
        { "softCleanup",  "Soft cleanup",          softCleanup },
        { "swellCleanup", "Ambient swell cleanup", swellCleanup },
    };
}

StudioGateBlock::StudioGateBlock()
    : GateBlock (typeId, "Studio Gate"),
      mode          (addParameter (choice ("mode", "Mode", { "Gate", "Expander", "Ducker", "Smart" }, (int) Mode::gate))),
      threshold     (addParameter ({ "threshold", "Threshold", { -90.0f, 0.0f }, -60.0f, "dB", 1 })),
      smartOffset   (addParameter ({ "smartOffset", "Threshold (Smart)", { 0.0f, 30.0f }, 9.0f, "dB", 1 })),
      smartCurve    (addParameter (choice ("smartCurve", "Smart curve", { "Gate", "Expander" }, (int) SmartCurve::gate))),
      range         (addParameter ({ "range", "Range", { -90.0f, 0.0f }, -90.0f, "dB", 0 })),
      ratio         (addParameter ({ "ratio", "Ratio", skewed (1.5f, 20.0f, 4.0f), 4.0f, {}, 1, 0.03,
                                     [] (float r) { return "1:" + juce::String (r, r < 10.0f ? 1 : 0); } })),
      attack        (addParameter ({ "attack", "Attack", skewed (0.05f, 50.0f, 2.0f), 0.5f, "ms", 2 })),
      hold          (addParameter ({ "hold", "Hold", skewed (0.0f, 500.0f, 50.0f), 20.0f, "ms", 0 })),
      release       (addParameter ({ "release", "Release", skewed (5.0f, 2000.0f, 200.0f), 80.0f, "ms", 0 })),
      hysteresis    (addParameter ({ "hysteresis", "Hysteresis", { 0.0f, 20.0f }, 6.0f, "dB", 1 })),
      detector      (addParameter (choice ("detector", "Detector", { "Peak", "RMS" }, (int) Detector::peak))),
      detectorTime  (addParameter ({ "detectorTime", "Detector time", skewed (1.0f, 50.0f, 10.0f), 10.0f, "ms", 1 })),
      keyHp         (addParameter ({ "keyHp", "Key high-pass", skewed (10.0f, 2000.0f, 200.0f), 10.0f, "Hz", 0, 0.03,
                                     [] (float hz) { return hz < keyHighPassOffBelowHz ? juce::String ("Off") : juce::String (juce::roundToInt (hz)) + " Hz"; } })),
      keyLp         (addParameter ({ "keyLp", "Key low-pass", skewed (500.0f, 25000.0f, 5000.0f), 25000.0f, "Hz", 0, 0.03,
                                     [] (float hz) { return hz > keyLowPassOffAboveHz ? juce::String ("Off")
                                                          : hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; } })),
      key           (addParameter (choice ("key", "Key", { "Input", "Guitar", "Audio input", "Block" }, (int) Key::guitar))),
      stereoLink    (addParameter (choice ("stereoLink", "Stereo", { "Linked", "Independent" }, (int) Stereo::linked))),
      lookahead     (addParameter (choice ("lookahead", "Look-ahead", { "Off", "On" }, (int) LookAheadSwitch::off))),
      lookaheadTime (addParameter ({ "lookaheadTime", "Look-ahead time", { 0.5f, 5.0f }, 1.0f, "ms", 1 }))
{
}

std::span<const FactoryPreset> StudioGateBlock::getFactoryPresets() const noexcept
{
    return factoryPresets;
}

bool StudioGateBlock::isPresetParameter (const Parameter& p) const noexcept
{
    // The setup, not the sound (as the Pedal EQ's Quality).
    return &p != &stereoLink && &p != &lookahead && &p != &lookaheadTime;
}

int StudioGateBlock::getLookAheadFromParameters() const noexcept
{
    return lookahead.getChoiceIndex() == (int) LookAheadSwitch::on ? lookAheadSamplesFor (lookaheadTime.getValue()) : 0;
}

GateSetup StudioGateBlock::readSetup (int numSamples) noexcept
{
    GateSetup s;
    s.way = (GateWay) juce::jlimit (0, 3, mode.getChoiceIndex());
    s.thresholdDb = threshold.skip (numSamples);
    s.smartOffsetDb = smartOffset.skip (numSamples);
    s.smartExpander = smartCurve.getChoiceIndex() == (int) SmartCurve::expander;
    s.rangeDb = range.skip (numSamples);
    s.ratio = ratio.skip (numSamples);
    s.attackMs = attack.skip (numSamples);
    s.holdMs = hold.skip (numSamples);
    s.releaseMs = release.skip (numSamples);
    s.hysteresisDb = hysteresis.skip (numSamples);
    s.detector = detector.getChoiceIndex() == (int) Detector::rms ? dsp::DetectorMode::rms : dsp::DetectorMode::peak;
    s.detectorMs = detectorTime.skip (numSamples);

    const auto hp = keyHp.skip (numSamples);
    const auto lp = keyLp.skip (numSamples);
    s.keyHighPassHz = hp < keyHighPassOffBelowHz ? 0.0f : hp;
    s.keyLowPassHz = lp > keyLowPassOffAboveHz ? 0.0f : lp;

    s.key = (GateKey) juce::jlimit (0, 3, key.getChoiceIndex());
    s.stereoLinked = stereoLink.getChoiceIndex() == (int) Stereo::linked;
    s.lookAheadSamples = getLookAheadFromParameters();
    return s;
}

} // namespace encore
