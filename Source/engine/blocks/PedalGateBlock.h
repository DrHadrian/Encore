// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/blocks/GateBlock.h"

namespace encore
{

/** The Pedal Gate "S2" (an FX Unit; the Encore Engine; B2, GATE-PLAN §2): a
    smart gate pedal with two knobs. Everything else adapts to the playing
    (dsp::AdaptiveGate): the threshold sits Threshold above the learned noise
    floor, the release follows the note from Decay's base, a chatter guard
    holds a fluttering note open, and a 0.3 ms fade-in keeps the pick attack.

    - Threshold 0-10: 3 dB per step above the floor (0 to +30 dB).
    - Decay 0-10: the base release, 20 * 40^(knob / 10) ms (20 ms at 0,
      about 87 ms at 4, 800 ms at 10).
    - Detect (mini switch): Input, or Guitar (the patch's clean input while
      it cuts where it sits, as today's gate's "Detect guitar").
    - Mode (mini switch): Hard mutes (-90 dB); Soft is an expander 1:2 that
      turns the noise down 15 dB at most.

    Fixed inside: a Peak detector of 10 ms, 6 dB hysteresis, a base hold of
    20 ms, no key filter, stereo always linked. Hidden until U2 and the rig
    test; a show that uses it saves as schema 5.
*/
class PedalGateBlock final : public GateBlock
{
public:
    static constexpr auto typeId = "pedalGate";

    enum class Detect { input, guitar };
    enum class Mode { hard, soft };

    // Proposed values (GATE-PLAN §2), tuned on the rig in U2.
    static constexpr float thresholdDbPerStep = 3.0f;
    static constexpr float decayMinMs = 20.0f, decaySpan = 40.0f;   // 20 * 40^(knob/10)
    static constexpr float hardRangeDb = -90.0f;
    static constexpr float softRangeDb = -15.0f, softRatio = 2.0f;
    static constexpr float detectorMs = 10.0f, hysteresisDb = 6.0f;
    static constexpr float holdMs = 20.0f, attackMs = 0.3f;

    PedalGateBlock();

    static float offsetDbFor (float knob) noexcept   { return thresholdDbPerStep * juce::jlimit (0.0f, 10.0f, knob); }
    static float releaseMsFor (float knob) noexcept  { return decayMinMs * std::pow (decaySpan, juce::jlimit (0.0f, 10.0f, knob) / 10.0f); }

    /** The setup these settings give (shared with the X4's Smart presets). */
    static GateSetup setupFor (float thresholdKnob, float decayKnob, Detect detect, Mode mode) noexcept;

    std::span<const FactoryPreset> getFactoryPresets() const noexcept override;

private:
    GateSetup readSetup (int numSamples) noexcept override;

    Parameter& threshold;
    Parameter& decay;
    Parameter& detect;
    Parameter& soft;
};

} // namespace encore
