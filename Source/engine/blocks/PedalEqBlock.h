// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/dsp/Crossfade.h"
#include "engine/dsp/FilterBand.h"
#include "engine/dsp/Oversampler.h"
#include "engine/dsp/SoftClip.h"

#include <atomic>

#include <array>

namespace encore
{

/** The Pedal EQ "G7": a 7-band graphic EQ pedal, the first FX Unit (native
    module; its DSP is the Encore Engine's). Spec: claude/EQ-PLAN.md §3;
    decision 0063.

    Signal: Input (Pad / Normal / Boost) -> character capture (a NAM capture
    of a pedal's buffer, optional) -> seven bells at 100 Hz .. 6.4 kHz ->
    the 9 V soft clip (oversampled by Quality) -> Level.

    It keeps the channels it gets (mono in, mono out; stereo in, stereo out)
    and has no tail. Hidden from the Add block menu until its UI is done and
    the rig test has passed; a show that uses it is schema 5.

    Threads: as every block (Block): constructor and prepareBlock on the
    message thread, processBlock and resetBlock on the audio thread, no
    allocation or locks there.
*/
class PedalEqBlock final : public Block
{
public:
    static constexpr auto typeId = "pedalEq";

    static constexpr int numBands = 7;
    static constexpr std::array<double, numBands> bandFrequencies { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
    static constexpr std::array<const char*, numBands> bandIds { "g100", "g200", "g400", "g800", "g1k6", "g3k2", "g6k4" };
    static constexpr float sliderRangeDb = 15.0f;

    // Choices (their order is saved in shows: never reorder).
    enum class QMode { classic = 0, wide = 1 };
    enum class Input { pad = 0, normal = 1, boost = 2 };
    enum class CaptureLevel { asCaptured = 0, levelMatched = 1 };
    enum class RealClip { off = 0, on = 1 };

    // Classic Q is proportional (EQ-PLAN §3.1, like an analog gyrator graphic
    // EQ): about 1.3 octaves (Q 1.07) near 0 dB, narrowing linearly to about
    // 0.5 octave (Q 2.87) at +/-15 dB. Wide is a constant Q of 1.07.
    static constexpr double qAtZero = 1.07, qAtFullScale = 2.87;
    static constexpr int updateInterval = 16;   // samples between redesigns while a slider moves

    static constexpr float padDb = -6.0f, boostDb = 6.0f;

    /** The Input switch's gain (linear). Any thread. */
    static float inputGainFor (Input setting) noexcept;

    /** The band's Q for a gain, in each Q mode. Any thread. */
    static double qFor (QMode mode, double gainDb) noexcept;

    /** Band b's design at a gain, in a Q mode: what the audio plays, and what
        a curve on screen draws (dsp::responseDb of all seven). Any thread. */
    static dsp::FilterBandDesign designFor (int band, double gainDb, QMode mode, double sampleRate) noexcept;

    /** The seven designs as the parameters are set now (their targets), for a
        curve. Message thread (or any: it only reads the parameters). */
    std::array<dsp::FilterBandDesign, numBands> getBandDesigns (double sampleRate) const noexcept;

    static constexpr double hotHoldSeconds = 0.3;    // HOT stays on this long after the clip worked (Proposed)
    static constexpr double hotThresholdDb = -1.0;   // "worked" = more than 1 dB of gain reduction
    static constexpr double dcBlockerHz = 1.0;       // after the (asymmetric) clip: 0.01 dB at 20 Hz
    static constexpr int clipWarmupSamples = 128;    // history run through the clip stage before it comes in
    static constexpr double clipFadeSeconds = 0.03;  // in/out of the path: a latency change, so a longer fade

    PedalEqBlock();

    /** The 9 V clip took off more than 1 dB within the last 300 ms: the UI
        shows HOT next to Level (U1). Any thread. */
    bool isHot() const noexcept { return hot.load (std::memory_order_relaxed); }

    /** Whether the clip stage is in the signal path now. Any thread. */
    bool isClipInPath() const noexcept { return clipInPath.load (std::memory_order_relaxed); }

    /** The clip stage's oversampling latency while it is in the path, else 0. Any thread. */
    int getLatencySamples() const noexcept override;

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    std::array<Parameter*, numBands> addBands();

    std::array<Parameter*, numBands> bands;   // first, so the sliders lead the parameter list
    Parameter& level;
    Parameter& qMode;
    Parameter& input;
    Parameter& quality;
    Parameter& captureLevel;
    Parameter& realClip;

    // Audio thread.
    void processInput (float* const* channels, int numChannels, int numSamples) noexcept;
    void processBands (float* const* channels, int numChannels, int numSamples) noexcept;
    void processLevel (float* const* channels, int numChannels, int numSamples) noexcept;
    void processClip (float* const* channels, int numChannels, int numSamples) noexcept;
    void runClip (float* const* channels, int numChannels, int numSamples) noexcept;
    void rememberClipInput (const float* const* channels, int numChannels, int numSamples) noexcept;
    void warmUpClip (int numChannels) noexcept;
    bool clipNeeded() const noexcept;

    // The Input switch moves its gain over 10 ms: for a gain, the same as a
    // crossfade between the two settings, at no extra cost.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGain;

    // The 9 V clip stage: oversampled soft clip, then a DC blocker. Only in
    // the path when it can work (or Real pedal clipping is on); going in or
    // out crossfades 10 ms.
    const dsp::SoftClip softClip;
    dsp::Oversampler oversampler;
    dsp::FilterBand<maxChannels> dcBlocker;
    dsp::Crossfade clipFade;
    bool clipWasInPath = false;
    std::atomic<bool> clipInPath { false };

    // The last samples that reached the clip stage while it was out of the
    // path: run through it just before it comes in, so its filters start
    // settled instead of from silence (which would click as it fades in).
    juce::AudioBuffer<float> clipHistory, clipWarmup;
    int maxBlock = 0;

    bool clipWorkedThisBlock = false;
    int hotSamplesLeft = 0, hotHoldSamples = 0;
    std::atomic<bool> hot { false };

    double sampleRate = 48000.0;
    std::array<dsp::FilterBand<maxChannels>, numBands> filters, oldFilters;
    std::array<double, numBands> appliedGains {};
    QMode appliedQ = QMode::classic;
    dsp::Crossfade qFade;
};

} // namespace encore
