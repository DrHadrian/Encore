// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/blocks/GateBlock.h"

namespace encore
{

/** The Studio Gate "X4" (an FX Unit; the Encore Engine; B2, GATE-PLAN §3):
    the gate with every control, four ways of working.

    - Mode: Gate (below the close level it turns down to Range), Expander
      (below the threshold the level falls by the Ratio, at most Range),
      Ducker (turns down to Range while the key is loud), Smart (the S2's
      adaptive gate: Threshold (Smart) above the learned floor, Release as
      its base; Smart curve Gate or Expander, Adrian 27 Nov).
    - Key: its own input, the guitar, another interface input (ref
      keyInput) or an earlier block (ref keyBlock); without its ref, its
      own input. A key filter (12 dB/oct high- and low-pass), Peak or RMS
      detection at the detector time, linked or independent stereo.
    - Look-ahead 0.5-5 ms: the audio is delayed so the gate opens before the
      pick attack arrives; reported as latency while on (split paths line up,
      0053); switching it or changing its time crossfades.
    - Key listen (edit only, not saved): GateBlock::setKeyListen.

    Type id "studioGate" while it's hidden: today's NoiseGateBlock keeps
    "gate" until U2 (BUILD-PLAN §1.6). Then "gate" opens as an X4 through
    convertOldGate and a saved "studioGate" reads as "gate". The defaults are
    today's gate's fixed values.
*/
class StudioGateBlock final : public GateBlock
{
public:
    static constexpr auto typeId = "studioGate";

    enum class Mode { gate, expander, ducker, smart };
    enum class SmartCurve { gate, expander };
    enum class Detector { peak, rms };
    enum class Key { input, guitar, audioInput, block };
    enum class Stereo { linked, independent };
    enum class LookAheadSwitch { off, on };

    static constexpr float keyHighPassOffBelowHz = 20.0f;    // 10 on the knob = Off
    static constexpr float keyLowPassOffAboveHz = 20000.0f;  // 25 k on the knob = Off

    StudioGateBlock();

    std::span<const FactoryPreset> getFactoryPresets() const noexcept override;
    bool isPresetParameter (const Parameter&) const noexcept override;

private:
    GateSetup readSetup (int numSamples) noexcept override;
    int getLookAheadFromParameters() const noexcept override;
    bool hasLookAhead() const noexcept override { return true; }

    Parameter& mode;
    Parameter& threshold;
    Parameter& smartOffset;
    Parameter& smartCurve;
    Parameter& range;
    Parameter& ratio;
    Parameter& attack;
    Parameter& hold;
    Parameter& release;
    Parameter& hysteresis;
    Parameter& detector;
    Parameter& detectorTime;
    Parameter& keyHp;
    Parameter& keyLp;
    Parameter& key;
    Parameter& stereoLink;
    Parameter& lookahead;
    Parameter& lookaheadTime;
};

} // namespace encore
