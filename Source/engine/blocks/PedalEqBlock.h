// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/BackgroundJobs.h"
#include "engine/core/Block.h"
#include "engine/core/RealtimeHandover.h"
#include "engine/dsp/Crossfade.h"
#include "engine/dsp/FilterBand.h"
#include "engine/dsp/Oversampler.h"
#include "engine/dsp/SoftClip.h"
#include "engine/nam/NamModel.h"

#include <atomic>

#include <array>

namespace encore
{

/** The Pedal EQ "G7": a 7-band graphic EQ pedal, the first FX Unit (native
    module; its DSP is the Encore Engine's). Spec: claude/EQ-PLAN.md §3;
    decision 0065.

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

    //== The character capture (EQ-PLAN §3.1, MODULES-PLAN §4) ================
    /** A NAM capture of a real pedal's buffer and op-amp stages, played after
        Input and before the bands. One copy per side (Adrian, 27 Nov: one per
        side for now; a setting later), so stereo stays stereo; mono uses the
        left. Empty = "DSP only". Built off the audio thread (loadCapture). */
    struct Capture
    {
        juce::File file;
        std::array<std::unique_ptr<NamModel>, maxChannels> sides;

        /** The gain that makes the capture's output as loud as its input
            (pink noise at -18 dBFS RMS, measured when loaded): "Level-matched". */
        float matchGain = 1.0f;

        bool isEmpty() const noexcept { return sides[0] == nullptr; }
        int getLatencySamples() const noexcept { return isEmpty() ? 0 : sides[0]->getLatencySamples(); }
        double getCpuLoadPerSide() const noexcept { return isEmpty() ? 0.0 : sides[0]->getMeasuredCpuLoad(); }

        /** For a new sample rate or buffer size. Off the audio thread. */
        void prepare (const ProcessSpec& spec);
    };

    static constexpr double captureFadeSeconds = 0.03;
    static constexpr float captureMatchLevelDb = -18.0f;

    /** The signal "Level-matched" is measured with: noise tilted towards
        guitar-like content, at -18 dBFS RMS, deterministic. A capture is
        matched so this comes out as loud as it goes in (a nonlinear capture
        matches exactly only at this level). Any thread except the audio thread. */
    static std::vector<float> captureMatchSignal (double sampleRate, double seconds = 0.5);

    /** Loads a capture (both sides, prepared and measured). Background or
        message thread, never the audio thread: it parses, allocates and
        prewarms. Null with the reason if it can't. */
    static std::unique_ptr<Capture> loadCapture (const juce::File& file, const ProcessSpec& spec, juce::String& error);

    /** Hands a capture (or an empty one: DSP only) to the audio thread; it
        crossfades 30 ms from the old one. Message thread. */
    void installCapture (std::unique_ptr<Capture> capture);

    /** For a block that isn't playing yet: active from the first sample. */
    void installCaptureBeforePlaying (std::unique_ptr<Capture> capture);

    /** Back to DSP only (message thread). */
    void unloadCapture() { installCapture (std::make_unique<Capture>()); }

    /** The capture the UI should describe (newest installed), or null when DSP only. */
    const Capture* getLatestCapture() const noexcept;

    /** Captures still in memory (playing, fading or waiting to be freed). */
    int getNumCapturesInMemory() const { return captures.getNumOwned(); }

    //== Block: files (message thread) =========================================
    const FileSlot* getFileSlot() const noexcept override { return &fileSlot; }
    void loadFileAsync (const juce::File& file, std::function<void (juce::String)> onDone) override;
    juce::File getLoadedFile() const override;
    juce::String getLoadedFileInfo() const override;
    void collectGarbage() override { captures.collectGarbage(); updateCaptureLatency(); }

    PedalEqBlock();
    ~PedalEqBlock() override;

    /** The 9 V clip took off more than 1 dB within the last 300 ms: the UI
        shows HOT next to Level (U1). Any thread. */
    bool isHot() const noexcept { return hot.load (std::memory_order_relaxed); }

    /** Whether the clip stage is in the signal path now. Any thread. */
    bool isClipInPath() const noexcept { return clipInPath.load (std::memory_order_relaxed); }

    /** The clip stage's oversampling latency while it is in the path, else 0. Any thread. */
    int getLatencySamples() const noexcept override;

    /** Flat, Mid push, Scoop, Clean boost, Bright, Thicken (EQ-PLAN §4;
        values Proposed, tuned by ear in U1). They set the face: the
        sliders, Level, Q and Input; not Quality, Capture level or Real
        pedal clipping (the pedal's setup). Any thread. */
    std::span<const FactoryPreset> getFactoryPresets() const noexcept override;
    bool isPresetParameter (const Parameter&) const noexcept override;

private:
    /** Message thread: the capture's latency, for getLatencySamples() on any thread. */
    void updateCaptureLatency() noexcept;
    std::atomic<int> captureLatency { 0 };

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
    void processCapture (float* const* channels, int numChannels, int numSamples) noexcept;
    void runCapture (const Capture* capture, float* const* channels, int numChannels, int numSamples, bool smoothedGain) noexcept;
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

    // The capture: handed over lock-free, crossfaded on a swap, freed on
    // the message thread (collectGarbage), as the amp's models are.
    const FileSlot fileSlot { juce::String (juce::CharPointer_UTF8 ("Load capture\xe2\x80\xa6")), "*.nam" };
    RealtimeHandover<Capture, 8> captures;
    Capture* currentCapture = nullptr;
    Capture* fadingCapture = nullptr;
    dsp::Crossfade captureFade;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> captureGain;
    juce::SharedResourcePointer<BackgroundJobs> jobs;

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
