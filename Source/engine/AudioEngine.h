// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

#include "engine/PeakMeter.h"
#include "engine/Tuner.h"
#include "engine/core/OutputStrip.h"
#include "engine/core/PatchPlayer.h"
#include "engine/midi/MidiRouter.h"
#include "engine/dsp/SafetyLimiter.h"

namespace encore
{

/** The real-time heart of Encore.

    Signal path (milestone 11, routing): this computer's named inputs -> patch
    player (active patch + patches ringing out, each from its own input, into
    every output through its own sends) -> per output: master gain/mute/tuner
    mute and panic fade (shared), the output's level and mute, EQ, delay,
    safety limiter, hard ceiling -> the output's channels on the interface.
    The tuner listens to the playing patch's input.

    Threading contract (applies to every engine class):
      - audioDeviceIOCallbackWithContext() runs on the audio thread. It never
        allocates, locks, logs or touches files.
      - Every other public function is for the message (UI) thread. Setters
        only write atomics or queue work for the audio thread.
      - audioDeviceAboutToStart()/audioDeviceStopped() are called by JUCE while
        no audio callback is running, so they may allocate and prepare.
*/
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();

    //== Patches (message thread) ==============================================
    /** Owns every patch's chain and switches between them (see PatchPlayer). */
    PatchPlayer& getPlayer() noexcept { return player; }

    /** Shortcut for a single chain (tests, simple setups): adopts it, makes it
        the only playable chain and plays it. Releases the previous one. */
    Chain* setChain (std::unique_ptr<Chain> chain);

    /** Foot controller and pedal input, and cue MIDI out. */
    MidiRouter& getMidiRouter() noexcept { return midi; }

    /** The chain the audio thread is playing (what the UI shows). */
    Chain* getChain() const noexcept { return player.getActiveChain(); }

    /** The device's current sample rate and buffer size (invalid while stopped). */
    ProcessSpec getCurrentSpec() const noexcept { return { currentSampleRate.load(), currentBlockSize.load() }; }

    /** Frees chains, models, IRs the audio thread has finished with. Call from a UI timer. */
    void collectGarbage();

    //== Controls (message thread) =============================================
    void setMasterGainDecibels (float dB) noexcept;
    void setMuted (bool shouldBeMuted) noexcept;
    bool isMuted() const noexcept { return muted.load (std::memory_order_relaxed); }

    //== Tuner and panic (any thread) ==========================================
    Tuner& getTuner() noexcept { return tuner; }
    void setTunerOpen (bool shouldBeOpen) noexcept { tuner.setActive (shouldBeOpen); }
    bool isTunerOpen() const noexcept              { return tuner.isActive(); }

    /** Whether an open tuner silences the output (the default) or lets it play. */
    void setTunerMutes (bool shouldMute) noexcept  { tunerMutes.store (shouldMute, std::memory_order_relaxed); }
    bool getTunerMutes() const noexcept            { return tunerMutes.load (std::memory_order_relaxed); }

    enum class PanicMode
    {
        recover,   // silence, clear every tail, then come back on the same cue by itself
        latch      // silence until panic is pressed again
    };

    enum class PanicState { none, silencing, silent, returning };

    /** Panic: fades the output out in a few ms, drops everything ringing and
        clears the patch's delays. In latch mode a second press brings it back. */
    void panic() noexcept { panicRequests.fetch_add (1, std::memory_order_relaxed); }
    void setPanicMode (PanicMode mode) noexcept { panicMode.store (mode, std::memory_order_relaxed); }
    PanicMode getPanicMode() const noexcept     { return panicMode.load (std::memory_order_relaxed); }
    PanicState getPanicState() const noexcept   { return panicState.load (std::memory_order_relaxed); }

    static constexpr double panicFadeSeconds = 0.005, panicHoldSeconds = 0.3, panicReturnSeconds = 0.5;

    //== Routing (message thread) ==============================================
    /** This computer's inputs and outputs. Takes effect on the next buffer,
        without a click: levels and sends ramp, the EQ glides, a delay change
        crossfades, a new output fades in. Outputs keep their slot (and state)
        by name. The default routing plays exactly as before routing. */
    void setRouting (const RoutingConfig& config);
    const RoutingConfig& getRouting() const noexcept { return routingConfig; }

    /** The slot of the input a patch asking for this name plays (that input,
        or the first when the name isn't here). Always valid. */
    int getInputSlot (const juce::String& inputName) const noexcept;

    /** The slot of an output, -1 when this computer doesn't have it. */
    int getOutputSlot (const juce::String& outputName) const noexcept;

    /** Sets a chain's input and sends from its patch: the input by name, each
        output's send as the patch says (0 dB when it doesn't mention it).
        Call when a chain is built and whenever the routing or the patch's
        sends change. */
    void applyPatchRouting (Chain& chain, const juce::String& inputName,
                            const std::function<float (const juce::String& outputName)>& sendGainFor) const;

    /** An output's mute, any thread (Stage, the bottom row, a footswitch).
        The output keeps it while the routing changes. */
    void setOutputMuted (int outputSlot, bool shouldBeMuted) noexcept;
    bool isOutputMuted (int outputSlot) const noexcept;

    /** Message thread: takes mutes made elsewhere (Stage, a footswitch) into
        getRouting(). True if any changed (then save the routing). */
    bool syncOutputMutes();

    /** An output's level (dB, -inf..+6), any thread (a fader while it moves). */
    void setOutputLevel (int outputSlot, float dB) noexcept;

    /** The interface's channels, as the device last started: how many it has,
        and whether a (1-based) channel can be used (it exists and is active). */
    int getDeviceInputChannels() const noexcept   { return deviceInputs.load (std::memory_order_relaxed); }
    int getDeviceOutputChannels() const noexcept  { return deviceOutputs.load (std::memory_order_relaxed); }
    bool isInputChannelUsable (int channel) const noexcept;
    bool isOutputChannelUsable (int channel) const noexcept;

    //== Monitoring (message thread) ===========================================
    /** The playing patch's input, and every output together (loudest). */
    PeakMeter& getInputMeter() noexcept  { return inputMeter; }
    PeakMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Per input and output slot (outputs: left and right). */
    PeakMeter& getInputSlotMeter (int slot) noexcept;
    PeakMeter& getOutputSlotMeter (int slot, int channel) noexcept;

    /** An output's deepest limiter gain reduction since the last call (dB). */
    float takeOutputLimiterReductionDb (int slot) noexcept;

    /** Buffers in which an output's hard ceiling had to act (since the counters were reset). */
    int getOutputClipCount (int slot) const noexcept;

    /** Buffers of broken audio silenced in the outputs (should stay 0). */
    int getBadOutputBufferCount() const noexcept;

    /** Fraction of each buffer's time the whole callback needs (1.0 = 100 %). */
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }

    /** Callbacks that took longer than their buffer: an audible dropout risk. */
    int getOverrunCount() const noexcept { return loadMeasurer.getXRunCount(); }

    /** Deepest safety-limiter gain reduction since the last call, over every
        output (dB, 0 = idle). */
    float takeLimiterReductionDb() noexcept;

    /** Blocks where the output still hit the final hard clamp (should stay 0). */
    int getClipCount() const noexcept { return clipCount.load (std::memory_order_relaxed); }

    void resetCounters() noexcept;

    /** Counts audio callbacks: if it stops moving while a device is open, the
        driver has stalled or the interface was unplugged. */
    juce::uint32 getCallbackCount() const noexcept { return callbacks.load (std::memory_order_relaxed); }

    //== juce::AudioIODeviceCallback ===========================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    static void clearOutputs (float* const* outputs, int numOutputs, int numSamples) noexcept;
    void adoptRoutingTable() noexcept;
    void renderChunk (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                      int offset, int numSamples) noexcept;
    void handlePanicAndTunerRequests() noexcept;
    void advancePanic (int numSamples) noexcept;

    std::atomic<float> masterGainDb { -12.0f };
    std::atomic<bool> muted { true };              // always start muted: safest on a stage

    // Routing, message-thread side: the config and which name has which slot.
    RoutingConfig routingConfig;
    std::array<juce::String, (size_t) routing::maxInputs> inputSlotNames;
    std::array<juce::String, (size_t) routing::maxOutputs> outputSlotNames;
    RealtimeHandover<RoutingTable, 8> routingTables;

    // Per output slot, any thread.
    std::array<std::atomic<float>, (size_t) routing::maxOutputs> outputLevelGain;
    std::array<std::atomic<bool>, (size_t) routing::maxOutputs> outputMuted;
    std::array<std::atomic<int>, (size_t) routing::maxOutputs> outputClips {};

    // The interface as it last started (written while audio is stopped).
    std::atomic<int> deviceInputs { 0 }, deviceOutputs { 0 };
    std::atomic<juce::uint64> usableInputs { 0 }, usableOutputs { 0 };   // bit n = channel n + 1
    std::array<int, (size_t) routing::maxChannel> inputActiveIndex {}, outputActiveIndex {};   // -1 = not active

    PatchPlayer player;
    MidiRouter midi;
    Tuner tuner;

    std::atomic<bool> tunerMutes { true };
    std::atomic<int> panicRequests { 0 };
    std::atomic<PanicMode> panicMode { PanicMode::recover };
    std::atomic<PanicState> panicState { PanicState::none };

    // Audio-thread-only panic state.
    float panicGain = 1.0f, panicStep = 0.0f;
    bool panicMoving = false;
    int panicHeld = 0, seenPanicRequests = 0, seenMidiPanics = 0, seenMidiTuners = 0;
    Chain* singleChain = nullptr;   // the chain set by setChain(), if used

    // Audio-thread-only state, (re)allocated only in audioDeviceAboutToStart().
    RoutingTable* routingTable = nullptr;
    juce::AudioBuffer<float> buses;                // per output slot: left, right
    std::vector<float> sharedGain;                 // master x mute x tuner x panic, per sample
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 0.0f };
    std::array<OutputStrip, (size_t) routing::maxOutputs> strips;

    PeakMeter inputMeter, outputMeter;
    std::array<PeakMeter, (size_t) routing::maxInputs> inputSlotMeters;

    // Its audio-side calls only *try* to lock and skip a measurement when busy,
    // so the UI may reset it while audio runs without risking a stall.
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<int> clipCount { 0 };
    std::atomic<juce::uint32> callbacks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace encore
