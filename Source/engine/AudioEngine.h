// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

#include "engine/PeakMeter.h"

namespace encore
{

/** The real-time heart of Encore.

    Milestone 0: takes one guitar input channel, applies the master volume/mute
    and sends it to every active output channel (dry passthrough).

    Threading contract (applies to every engine class from here on):
      - audioDeviceIOCallbackWithContext() runs on the audio thread. It never
        allocates, locks, logs or touches files.
      - Every public setter/getter is safe to call from the UI thread at any time.
        They only read or write atomics.
      - audioDeviceAboutToStart()/audioDeviceStopped() are called by JUCE while
        no audio callback is running, so they may prepare state.
*/
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();

    //== Controls (UI thread) ==================================================
    void setMasterGainDecibels (float dB) noexcept;
    void setMuted (bool shouldBeMuted) noexcept;
    bool isMuted() const noexcept { return muted.load (std::memory_order_relaxed); }

    /** Which of the device's *active* input channels carries the guitar (0-based). */
    void setGuitarInputChannel (int activeChannelIndex) noexcept;
    int getGuitarInputChannel() const noexcept { return guitarInput.load (std::memory_order_relaxed); }

    //== Monitoring (UI thread) ================================================
    PeakMeter& getInputMeter() noexcept  { return inputMeter; }
    PeakMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Fraction of each buffer's time the callback needs (1.0 = 100 %). */
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }

    /** Callbacks that took longer than their buffer: an audible dropout risk. */
    int getOverrunCount() const noexcept { return loadMeasurer.getXRunCount(); }

    /** Callbacks where output was hard-clipped by the safety clamp. */
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

    std::atomic<float> masterGainDb { -12.0f };
    std::atomic<bool> muted { true };              // always start muted: safest on a stage
    std::atomic<int> guitarInput { 0 };

    // Audio-thread-only state. A linear ramp (not an instant jump) on every
    // gain change is what keeps volume moves and mute from clicking.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 0.0f };

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
