// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

#include "engine/PeakMeter.h"
#include "engine/core/ChainHandover.h"
#include "engine/dsp/SafetyLimiter.h"

namespace encore
{

/** The real-time heart of Encore.

    Signal path: guitar input -> active chain -> master gain/mute -> safety
    limiter -> every active output.

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

    //== Chain (message thread) ================================================
    /** Hands a new chain to the audio thread. It becomes active on the next callback. */
    void setChain (std::unique_ptr<Chain> chain);

    /** The chain the UI should show and edit. */
    Chain* getChain() const noexcept { return chains.getLatest(); }

    /** The device's current sample rate and buffer size (invalid while stopped). */
    ProcessSpec getCurrentSpec() const noexcept { return { currentSampleRate.load(), currentBlockSize.load() }; }

    /** Frees chains the audio thread has finished with. Call from a UI timer. */
    void collectGarbage() { chains.collectGarbage(); }

    //== Controls (message thread) =============================================
    void setMasterGainDecibels (float dB) noexcept;
    void setMuted (bool shouldBeMuted) noexcept;
    bool isMuted() const noexcept { return muted.load (std::memory_order_relaxed); }

    /** Which of the device's *active* input channels carries the guitar (0-based). */
    void setGuitarInputChannel (int activeChannelIndex) noexcept;
    int getGuitarInputChannel() const noexcept { return guitarInput.load (std::memory_order_relaxed); }

    //== Monitoring (message thread) ===========================================
    PeakMeter& getInputMeter() noexcept  { return inputMeter; }
    PeakMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Fraction of each buffer's time the whole callback needs (1.0 = 100 %). */
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }

    /** Callbacks that took longer than their buffer: an audible dropout risk. */
    int getOverrunCount() const noexcept { return loadMeasurer.getXRunCount(); }

    /** Deepest safety-limiter gain reduction since the last call (dB, 0 = idle). */
    float takeLimiterReductionDb() noexcept { return limiter.takeGainReductionDb(); }

    /** Blocks where the output still hit the final hard clamp (should stay 0). */
    int getClipCount() const noexcept { return clipCount.load (std::memory_order_relaxed); }

    void resetCounters() noexcept;

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
    void renderChunk (const float* input, float* const* outputs, int numOutputs,
                      int offset, int numSamples, Chain* chain) noexcept;

    std::atomic<float> masterGainDb { -12.0f };
    std::atomic<bool> muted { true };              // always start muted: safest on a stage
    std::atomic<int> guitarInput { 0 };

    ChainHandover chains;

    // Audio-thread-only state, (re)allocated only in audioDeviceAboutToStart().
    juce::AudioBuffer<float> work;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 0.0f };
    dsp::SafetyLimiter limiter;

    PeakMeter inputMeter, outputMeter;

    // Its audio-side calls only *try* to lock and skip a measurement when busy,
    // so the UI may reset it while audio runs without risking a stall.
    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<int> clipCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace encore
