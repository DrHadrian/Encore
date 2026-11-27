// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/dsp/Crossfade.h"
#include "engine/dsp/FilterBand.h"
#include "engine/dsp/Loudness.h"

#include <array>
#include <atomic>
#include <vector>

namespace encore
{

/** The Studio EQ "Q24": a parametric EQ with 24 band slots (native module,
    an FX Unit; its DSP is the Encore Engine's). Spec: claude/EQ-PLAN.md §2;
    stage B3 (decision 0067) builds its core: the bands in Zero latency, the
    output tools, presets and A/B data.

    Every slot's parameters exist all the time (bNNUsed, bNNFreq ...), so
    presets, expression and scenes point at a slot; adding a band takes a
    free slot (a model edit, PatchBuilder.h). A free slot, a band that is off
    and a band that changes nothing cost nothing.

    Signal: the bands in use, in slot order, in series (gain scale inside
    them) -> auto gain -> output -> pan / width (stereo) -> invert. It keeps
    the channels it gets; no tail, no latency (Zero latency is the only phase
    mode until B5).

    Parameters B4, B5 and R1 bring to life (dynamics, placement, character,
    Quality, phase) are registered already, saved and loaded, and change
    nothing yet.

    Hidden from the Add block menu until its UI (U3) and the rig test; a
    show that uses it is schema 5.

    Threads: as every block: the constructor and prepareBlock on the message
    thread, processBlock and resetBlock on the audio thread (no allocation,
    no locks; it only designs, glides and plays). The getters marked "any
    thread" read atomics.
*/
class StudioEqBlock final : public Block
{
public:
    static constexpr auto typeId = "studioEq";
    static constexpr int numSlots = 24;

    // Choices: their order is saved in shows, never reorder.
    enum class Shape { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf, allPass };
    static constexpr int numSlopes = 10;
    static constexpr std::array<int, numSlopes> slopeValues { 6, 12, 18, 24, 30, 36, 48, 72, 96, 96 };
    static constexpr int brickwallSlope = 9;   // plays as 96 dB/oct until Linear phase (B5)
    enum class Place { stereo, left, right, mid, side };
    enum class Mode { staticBand, dynamic, spectral };
    enum class Side { thisBand, dryGuitar };
    enum class SideFilter { follows, own };
    enum class Phase { zeroLatency, natural, linear };
    enum class LinearQuality { low, medium, high };
    enum class Character { clean, soft, warm };
    enum class Quality { live, high, max };
    enum class AutoGain { off, curve, smart };
    enum class CpuTight { keepTheSound, makeItLighter };
    static constexpr std::array<int, 4> displayRanges { 3, 6, 12, 30 };

    static constexpr float defaultFrequency = 1000.0f;
    static constexpr float thresholdAutoBelowDb = -60.5f;   // the Thresh knob's bottom (-61) reads Auto

    /** "b07Gain": slot is 0-based, the id 1-based and two digits. */
    static juce::String slotId (int slot, const char* suffix);

    /** One slot's band as the parameters say. */
    struct BandSettings
    {
        bool used = false, on = true;
        Shape shape = Shape::bell;
        double frequency = defaultFrequency, gainDb = 0.0, q = 1.0;
        int slope = 1;   // index into slopeValues
    };

    /** A slot's settings as the engine designs them (gain scale applied to
        bells, shelves and tilt; Brickwall as 96 dB/oct). Any thread. */
    static dsp::FilterSettings filterSettingsFor (const BandSettings& band, double gainScalePercent) noexcept;

    /** The designs of the bands that play, in slot order, as the parameters
        are set now (their targets), for a curve: dsp::responseDb of these is
        what the audio plays once it settles. Message thread (it reads atomics). */
    std::vector<dsp::FilterBandDesign> getBandDesigns (double sampleRate) const;

    /** The settings of slot (0-based), as the parameters are set now. Any thread. */
    BandSettings getBand (int slot) const noexcept;

    /** How many slots are in use. Any thread. */
    int getNumBandsInUse() const noexcept;

    /** The parameters of one slot, in the plan's order (EQ-PLAN §2.9). */
    struct SlotParameters
    {
        Parameter *used, *on, *shape, *freq, *gain, *q, *slope;
        Parameter *place, *mode, *range, *thresh, *attack, *release, *side, *sideFilter;   // B4
    };

    const SlotParameters& getSlotParameters (int slot) const noexcept { return slots[(size_t) slot].p; }

    //== The output tools (EQ-PLAN §2.5) =========================================
    // Order: bands (gain scale inside) -> auto gain -> output -> pan / width
    // (stereo only) -> invert.

    /** Auto gain, Curve: the gain that gives pink noise from 80 Hz to 8 kHz
        (a guitar's range) the same RMS after the bands as before, from the
        response at 48 log-spaced points, recomputed every 10 ms while a band
        moves (Proposed). Any thread. */
    static constexpr double curveLowHz = 80.0, curveHighHz = 8000.0;
    static constexpr int curvePoints = 48;
    static constexpr double curveIntervalSeconds = 0.01;
    static double curveGainDb (std::span<const dsp::FilterBandDesign> designs, double sampleRate) noexcept;

    /** Auto gain, Smart (Proposed, tuned on the rig in U3): starts from the
        Curve value and corrects it by the difference in K-weighted loudness
        (BS.1770) between the EQ's input and the bands' output over 3 s; only
        while the input is above -45 LUFS, over the window and in the last
        100 ms (someone playing now), and the tuner is closed; at most
        1 dB per second, within +-6 dB of the Curve value. */
    static constexpr double smartWindowSeconds = 3.0, smartGateLufs = -45.0;
    static constexpr double smartRateDbPerSecond = 1.0, smartLimitDb = 6.0;

    static constexpr double autoGainRampSeconds = 0.02;   // how the applied auto gain moves (and a mode switch crossfades)
    static constexpr double invertFadeSeconds = 0.01;

    /** The auto gain applied now, dB (0 when Off). Any thread. */
    float getAutoGainDb() const noexcept { return autoGainNow.load (std::memory_order_relaxed); }

    static constexpr double toggleSeconds = 0.01;    // a band on, off, in or out of use glides to or from flat
    static constexpr int updateInterval = 16;        // samples between redesigns while a value moves

    StudioEqBlock();

    std::span<const FactoryPreset> getFactoryPresets() const noexcept override;
    bool isPresetParameter (const Parameter& parameter) const noexcept override;

private:
    void prepareBlock (const ProcessSpec& spec) override;
    void resetBlock() noexcept override;
    void processBlock (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept override;

    struct Slot
    {
        SlotParameters p {};
        dsp::FilterBand<maxChannels> band, oldBand;   // oldBand: a Shape or Slope change's old setting, while it crossfades
        dsp::Crossfade fade;
        bool playing = false;                         // in use and on, as last applied
        int shape = -1, slope = -1;                   // as last applied
        int toggleRemaining = 0;                      // samples left of a glide to or from flat

        // Freq and Q ramp in log steps, as a knob turns (the parameter's own
        // ramp is linear: 30 Hz -> 30 kHz would jump octaves per chunk at first).
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> freqRamp, qRamp;

        bool valuesMoving() const noexcept { return freqRamp.isSmoothing() || qRamp.isSmoothing() || p.gain->isSmoothing(); }
        void snapValues() noexcept
        {
            freqRamp.setCurrentAndTargetValue (p.freq->getValue());
            qRamp.setCurrentAndTargetValue (p.q->getValue());
            p.gain->snapToTarget();
        }
    };

    /** The design of a slot at its parameters' current (ramped) values. Audio thread. */
    dsp::FilterBandDesign designNow (const Slot& slot, double gainScale) const noexcept;
    static bool isScaled (int shape) noexcept;

    /** Picks up on/off, in/out of use, Shape and Slope. Audio thread. */
    void applyChoices (Slot& slot, double gainScale) noexcept;
    void processSlot (Slot& slot, float* const* channels, int numChannels, int numSamples) noexcept;

    /** Audio thread: the Curve value from the bands' current designs (cheap:
        precomputed cosines, |H|^2 per section). */
    double curveNow() const noexcept;
    void processOutput (float* const* channels, int numChannels, int numSamples, bool tunerOpen) noexcept;
    void processBands (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept;

    std::array<Slot, numSlots> slots;
    Parameter& phase;
    Parameter& linearQuality;
    Parameter& character;
    Parameter& drive;
    Parameter& quality;
    Parameter& output;
    Parameter& pan;
    Parameter& width;
    Parameter& autoGain;
    Parameter& gainScale;
    Parameter& invert;
    Parameter& cpuTight;
    Parameter& displayRange;

    double sampleRate = 48000.0;
    int toggleSamples = 480;

    // Output tools, audio thread.
    std::array<double, curvePoints> pointCos {}, pointCos2 {};
    bool curveDirty = true;
    int curveCountdown = 0, curveIntervalSamples = 480;
    double curveDb = 0.0, smartCorrectionDb = 0.0;
    dsp::SlidingLoudness inputLoudness, bandsLoudness;
    juce::SmoothedValue<float> autoGainDb, outputGain, panLeft, panRight, widthAmount, polarity;
    std::atomic<float> autoGainNow { 0.0f };
    AutoGain lastAutoGainMode = AutoGain::off;
};

} // namespace encore
