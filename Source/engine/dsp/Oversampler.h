// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

#include "engine/dsp/Crossfade.h"

namespace encore::dsp
{

/** The Quality switch of modules that saturate (MODULES-PLAN §3.6). */
enum class Quality
{
    live = 0,   // 2x oversampling, the default
    high = 1,   // 4x
    max = 2     // 8x
};

/** Oversampling for a module's nonlinear stage, with the Quality switch
    (the Encore Engine's DSP, decision 0062).

    All three factors are built and prepared in prepare(), for the largest
    block, so switching Quality while playing allocates nothing. A switch
    crossfades 10 ms from the old factor to the new one (both run during the
    fade; the new one starts from a clean state).

    Filters: JUCE's polyphase IIR half-band filters (Adrian, 26 Nov: lowest
    latency for live use), with integer latency so the block can report it
    exactly and a split can align its paths: getLatencySamples() changes with
    the Quality.

    The nonlinear stage is a callback the module passes to process():
        void (float* const* channels, int numChannels, int numSamples,
              double oversampledRate, int qualityIndex)
    qualityIndex (0..2) lets a stage with its own state keep one per path.

    Threads: prepare() off the audio thread (allocates). setQuality(),
    process() and reset() on the audio thread: no allocation, no locks.
    getLatencySamples() from any thread.
*/
class Oversampler
{
public:
    static constexpr int numQualities = 3;

    static int factorFor (Quality q) noexcept { return 2 << (int) q; }   // 2, 4, 8

    void prepare (int maxChannels, int maxBlockSize, double sampleRate);

    /** Takes effect with a 10 ms crossfade at the next process(); a change
        during a fade waits for it to end. */
    void setQuality (Quality q) noexcept { requested = q; }

    /** Plays q from now on, with no fade (a reset: nothing is playing yet,
        e.g. a module that starts at its saved Quality). Audio thread, or
        before playing. */
    void resetTo (Quality q) noexcept
    {
        requested = current = previous = q;
        latency.store (latencies[(size_t) q], std::memory_order_relaxed);
        reset();
    }

    Quality getQuality() const noexcept { return current; }
    int getFactor() const noexcept      { return factorFor (current); }

    /** The latency of the Quality now playing, in samples at the base rate. */
    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    void reset() noexcept;

    template <typename NonlinearStage>
    void process (float* const* channels, int numChannels, int numSamples, NonlinearStage&& stage) noexcept
    {
        if (stages[0] == nullptr)
            return;   // not prepared: pass through rather than crash

        if (requested != current && ! fade.isActive())
        {
            previous = current;
            current = requested;
            stages[(size_t) current]->reset();
            latency.store (latencies[(size_t) current], std::memory_order_relaxed);
            fade.start();
        }

        const auto fading = fade.isActive();

        if (fading)
        {
            fade.copyInput (channels, numChannels, numSamples);
            run (previous, fade.oldChannels(), numChannels, numSamples, stage);
        }

        run (current, channels, numChannels, numSamples, stage);

        if (fading)
            fade.mix (channels, numChannels, numSamples);
    }

private:
    template <typename NonlinearStage>
    void run (Quality q, float* const* channels, int numChannels, int numSamples, NonlinearStage& stage) noexcept
    {
        auto& os = *stages[(size_t) q];
        juce::dsp::AudioBlock<float> block (channels, (size_t) numChannels, (size_t) numSamples);
        auto up = os.processSamplesUp (block);

        std::array<float*, 8> upChannels {};

        for (size_t ch = 0; ch < up.getNumChannels() && ch < upChannels.size(); ++ch)
            upChannels[ch] = up.getChannelPointer (ch);

        stage (upChannels.data(), numChannels, (int) up.getNumSamples(), baseRate * factorFor (q), (int) q);
        os.processSamplesDown (block);
    }

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, numQualities> stages;
    std::array<int, numQualities> latencies {};
    std::atomic<int> latency { 0 };
    Crossfade fade;
    double baseRate = 48000.0;
    Quality current = Quality::live, previous = Quality::live, requested = Quality::live;
};

} // namespace encore::dsp
