// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/core/LinkBus.h"
#include "engine/core/LockFreeFifo.h"
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

    /** The display feed (GATE-PLAN §7): while active, the audio thread pushes
        a frame about every 5 ms; off, it costs nothing. The UI (U2) pops them
        on its timer and draws the 4 s history. setDisplayActive: any thread;
        popDisplayFrame: one consumer thread (the message thread). */
    struct DisplayFrame
    {
        float inputDb = -200.0f;      // peak of what came in (lined up with the output)
        float keyDb = -200.0f;        // peak of the key, as the detector hears it (filtered)
        float outputDb = -200.0f;     // peak of what goes out
        float reductionDb = 0.0f;     // the most gain reduction in the frame (positive dB)
        bool open = true;             // the decision at the frame's end
    };

    static constexpr double displayFrameSeconds = 0.005;   // Proposed
    static constexpr int displayFifoFrames = 512;          // 2.5 s of frames: the UI drains at 30 Hz

    void setDisplayActive (bool shouldBeActive) noexcept { displayActive.store (shouldBeActive, std::memory_order_relaxed); }
    bool isDisplayActive() const noexcept                { return displayActive.load (std::memory_order_relaxed); }
    bool popDisplayFrame (DisplayFrame& frame) noexcept  { return displayFrames.pop (frame); }

    /** The look-ahead's samples while it's on (the X4), else 0. */
    int getLatencySamples() const noexcept override;

    /** Where the key comes from right now: as asked, or fell back to the
        gate's own input, and why (B2 step 4). Any thread. */
    enum class KeyState
    {
        inUse,            // the key it's set to (its own input, the guitar, an input, a block)
        inputNotHere,     // Audio input: the named input isn't on this computer
        noInputPicked,    // Audio input, but no input named yet
        blockNotUsable,   // Block: the block is missing, or not before this gate
        noBlockPicked     // Block, but no block picked yet (a ducking preset, until U2)
    };

    KeyState getKeyState() const noexcept { return (KeyState) keyStateNow.load (std::memory_order_relaxed); }

    /** One line for the block's info when the key fell back ("" when it
        didn't): "Key: Input 3 isn't on this computer". Message thread. */
    juce::String getKeyInfo() const;

    /** Linking (B2 step 6, GATE-PLAN §4): a follower shares an earlier gate's
        detection (its level, its open/closed decision and its threshold)
        and keeps its own Range and Release (or Decay). Any thread. */
    enum class LinkState
    {
        none,        // not linked
        following,   // its leader decides
        leaderOff,   // the leader didn't run (off, asleep, skipped): its own detector, the last shared threshold
        refused      // the link didn't resolve (a later gate, not a gate, missing)
    };

    LinkState getLinkState() const noexcept { return (LinkState) linkStateNow.load (std::memory_order_relaxed); }

    /** "Link: leader off", or why the link was refused ("" otherwise). Message thread. */
    juce::String getLinkInfo() const;

    //== Keys and links (Block; message thread / any thread) ===================
    const LinkBus* getLinkBus() const noexcept override { return &bus; }
    void setLinkLeader (const LinkBus* leaderBus, const juce::String& problem) override;
    void setKeyTap (const float* const* tapChannels, int numTapChannels, const juce::String& problem) override;
    void setKeyInputSlot (int slot) noexcept override;

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
            channel. filtered (optional) receives the filtered key. With a
            shared decision (a leader's level and open flags, already lined
            up) lane 0 follows it instead of its own detector. bus (optional)
            receives lane 0's level and decision for a follower of this one. */
        void run (const float* const* key, int keyChannels, int numSamples, int numChannels,
                  float* const* gains, float* const* filtered, bool tunerOpen,
                  const float* sharedLevel = nullptr, const std::uint8_t* sharedOpen = nullptr,
                  LinkBus* bus = nullptr) noexcept;

        float thresholdDb() const noexcept;

        float filter (float x, int channel) noexcept;
    };

    void publishState() noexcept;
    float lastLeaderThreshold = 0.0f;
    bool haveLeaderThreshold = false;

    /** Lines the leader's decisions up with this block's input. */
    void delayDecisions (int delay, int numSamples) noexcept;

    double sampleRate = 48000.0;
    Engine current, old;
    int fadeLength = 1, fadePosition = 1;
    dsp::LookAhead lookAhead;

    juce::AudioBuffer<float> keyCopy, filteredKey, gains, oldGains;
    float listenMix = 0.0f, listenStep = 1.0f;
    std::atomic<bool> keyListen { false };

    /** Audio thread: frames for the display, while it's active. */
    void feedDisplay (const float* const* channels, int numChannels, const float* const* filtered, int keyChannels,
                      const float* const* gainsUsed, int gainChannels, int numSamples) noexcept;
    std::atomic<bool> displayActive { false };
    bool displayWasActive = false;
    LockFreeFifo<DisplayFrame, displayFifoFrames> displayFrames;
    juce::AudioBuffer<float> displayInput;   // |input| per sample, before the gain (sized in prepare)
    int frameSamples = 240, frameCount = 0;
    float frameIn = 0.0f, frameKey = 0.0f, frameOut = 0.0f, frameMinGain = 1.0f;

    const float* const* keyTap = nullptr;   // set before playing (Chain::prepare)
    int keyTapChannels = 0;
    juce::String keyTapProblem;             // message thread
    bool keyBlockNamed = false;             // a keyBlock ref was given (set before playing)
    std::atomic<int> keyInputSlot { -1 };
    std::atomic<bool> keyInputNamed { false };
    std::atomic<int> keyStateNow { (int) KeyState::inUse };

    LinkBus bus;                            // what this gate decided, for its followers
    const LinkBus* leader = nullptr;        // set before playing (Chain::prepare)
    juce::String linkProblem;               // message thread
    std::vector<float> decisionLevel, delayedLevel;   // the leader's, lined up (sized when linked)
    std::vector<std::uint8_t> decisionOpen, delayedOpen;
    int decisionWrite = 0;
    bool wasFollowing = false;
    std::atomic<int> linkStateNow { (int) LinkState::none };

    std::atomic<bool> openNow { true }, frozenNow { false };
    std::atomic<float> reductionNow { 0.0f }, floorNow { dsp::AdaptiveGate::initialFloorDb };
};

} // namespace encore
