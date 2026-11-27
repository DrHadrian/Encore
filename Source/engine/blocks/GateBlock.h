// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/dsp/AdaptiveGate.h"
#include "engine/dsp/LookAhead.h"
#include "engine/dsp/Svf.h"

namespace encore
{

/** What a gate listens to (its key). */
enum class GateKey
{
    input,        // its own input
    guitar,       // the patch's clean input (ProcessContext::chainInput)
    audioInput,   // another input of the interface (ref keyInput)
    block         // an earlier block's output (ref keyBlock)
};

/** The four ways a gate works (the X4's Mode; the S2 is always Smart). */
enum class GateWay
{
    gate,
    expander,
    ducker,
    smart
};

/** Everything a gate is set to for one audio block: the S2 fills it from its
    two knobs and two switches, the X4 from its parameters, so both run the
    same engine and an X4 Smart preset sounds exactly like the S2's. */
struct GateSetup
{
    GateWay way = GateWay::gate;
    float thresholdDb = -60.0f;       // gate, expander, ducker
    float smartOffsetDb = 9.0f;       // smart: above the learned floor
    bool smartExpander = false;       // smart: Soft (the expander at ratio and range) instead of Hard
    float rangeDb = -90.0f;
    float ratio = 4.0f;
    float attackMs = 0.5f;
    float holdMs = 20.0f;             // smart: the base hold
    float releaseMs = 80.0f;          // smart: the base release
    float hysteresisDb = 6.0f;
    dsp::DetectorMode detector = dsp::DetectorMode::peak;
    float detectorMs = 10.0f;
    float keyHighPassHz = 0.0f;       // 0 = off
    float keyLowPassHz = 0.0f;        // 0 = off
    GateKey key = GateKey::guitar;
    bool stereoLinked = true;
    int lookAheadSamples = 0;

    bool operator== (const GateSetup&) const = default;

    /** A change that switches what the engine does (not just how much):
        those crossfade from the old engine to the new one. */
    bool switchesFrom (const GateSetup& other) const noexcept
    {
        return way != other.way || smartExpander != other.smartExpander || detector != other.detector
               || key != other.key || stereoLinked != other.stereoLinked;
    }
};

/** The gate engine shared by the Pedal Gate "S2" and the Studio Gate "X4"
    (the Encore Engine's; B2, GATE-PLAN §2-§4).

    Signal: the key (its own input, the clean guitar, another interface
    input or an earlier block) passes the key filter (12 dB/oct high- and
    low-pass), then the detector (Peak or RMS), then the way's logic
    (dsp::GateEnvelope, or dsp::AdaptiveGate for Smart). The gain acts on the
    block's own channels, delayed by the look-ahead when it's on. Linked
    stereo: one detection from the louder key channel; independent: one per
    channel. Starts open, as today's gate, so switching it on mid-note
    doesn't mute it.

    A switch of way, detector, key or stereo crossfades for 10 ms: the old
    engine runs beside the new one and their gains are blended (both act on
    the same audio, so blending the gains is a true crossfade).

    Threads: prepare off the audio thread; process on it (no allocation, no
    locks); the state getters and setKeyListen() from any thread.
*/
class GateBlock : public Block
{
public:
    static constexpr double switchFadeSeconds = 0.01;
    static constexpr double listenFadeSeconds = 0.01;

    GateBlock (juce::String typeId, juce::String displayName);
    ~GateBlock() override;

    //== Any thread (the UI) ===================================================
    /** The gate is open (for the ducker: it is ducking; the S2's OPEN LED). */
    bool isOpen() const noexcept                { return openNow.load (std::memory_order_relaxed); }

    /** How far it turns down right now, dB (0 = not at all). */
    float getGainReductionDb() const noexcept   { return reductionNow.load (std::memory_order_relaxed); }

    /** Smart: the learned noise floor, and whether learning is frozen. */
    float getLearnedFloorDb() const noexcept    { return floorNow.load (std::memory_order_relaxed); }
    bool isLearningFrozen() const noexcept      { return frozenNow.load (std::memory_order_relaxed); }

    /** Key listen (edit only, not saved): the output becomes the filtered
        key on every channel, crossfaded in and out. */
    void setKeyListen (bool shouldListen) noexcept { keyListen.store (shouldListen, std::memory_order_relaxed); }
    bool isKeyListening() const noexcept           { return keyListen.load (std::memory_order_relaxed); }

    /** The look-ahead's samples while it's on (the X4), else 0. */
    int getLatencySamples() const noexcept override;

    //== Message thread, before the block plays ================================
    /** Starts Smart learning from a floor learned before (a rebuilt patch,
        B2 step 7). */
    void setLearnedFloorDb (float db) noexcept;

protected:
    /** Audio thread: the setup for this block of samples (advances ramps). */
    virtual GateSetup readSetup (int numSamples) noexcept = 0;

    /** Any thread: the look-ahead the parameters ask for, samples at the
        prepared rate (0 = off). The S2 has none. */
    virtual int getLookAheadFromParameters() const noexcept { return 0; }

    /** True if the block has a look-ahead at all (its line is kept written). */
    virtual bool hasLookAhead() const noexcept { return false; }

    double getSampleRate() const noexcept { return sampleRate; }
    int lookAheadSamplesFor (double milliseconds) const noexcept { return lookAhead.samplesFor (milliseconds); }

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    struct Lane
    {
        dsp::LevelDetector detector;
        dsp::GateEnvelope envelope;
        dsp::AdaptiveGate adaptive;
    };

    /** One complete gate: settings, filters, detectors and envelopes. Copyable,
        so a switch can keep the old one running beside the new one. */
    struct Engine
    {
        GateSetup setup;
        std::array<Lane, 2> lanes;
        dsp::Svf<2> highPass, lowPass;
        dsp::SvfCoefficients highPassCoefficients, lowPassCoefficients;
        float highPassHz = -1.0f, lowPassHz = -1.0f;

        void prepare (double rate);
        void configure (const GateSetup& newSetup, double rate) noexcept;
        void reset() noexcept;

        /** Gains for numSamples from the key: gains[0] (linked) or one per
            channel. filtered (optional) receives the filtered key. */
        void run (const float* const* key, int keyChannels, int numSamples, int numChannels,
                  float* const* gains, float* const* filtered, bool tunerOpen) noexcept;

        float filter (float x, int channel) noexcept;
    };

    void publishState() noexcept;

    double sampleRate = 48000.0;
    Engine current, old;
    int fadeLength = 1, fadePosition = 1;
    dsp::LookAhead lookAhead;

    juce::AudioBuffer<float> keyCopy, filteredKey, gains, oldGains;
    float listenMix = 0.0f, listenStep = 1.0f;
    std::atomic<bool> keyListen { false };

    std::atomic<bool> openNow { true }, frozenNow { false };
    std::atomic<float> reductionNow { 0.0f }, floorNow { dsp::AdaptiveGate::initialFloorDb };
};

} // namespace encore
