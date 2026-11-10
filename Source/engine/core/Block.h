// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

#include "engine/core/Parameter.h"

namespace encore
{

struct ProcessSpec
{
    double sampleRate = 0.0;
    int maxBlockSize = 0;

    bool isValid() const noexcept { return sampleRate > 0.0 && maxBlockSize > 0; }
    bool operator== (const ProcessSpec& other) const noexcept
    {
        return juce::exactlyEqual (sampleRate, other.sampleRate) && maxBlockSize == other.maxBlockSize;
    }
};

/** One processing unit in a chain: gate, EQ, and later NAM amp, cab IR, VST3...

    Subclasses implement prepareBlock/processBlock/resetBlock. The base class
    adds what every block needs and nobody should re-implement:
      - a click-free bypass (10 ms crossfade between dry and processed signal),
      - parameter ramps,
      - per-block CPU measurement shown in the UI.

    Threads: prepare() runs off the audio thread while the block isn't playing
    (it may allocate). process()/reset() run on the audio thread and must not
    allocate, lock or do I/O. setEnabled() and the parameters are safe from any thread.
*/
class Block
{
public:
    static constexpr int maxChannels = 2;

    Block (juce::String typeId, juce::String displayName);
    virtual ~Block();

    //== Any thread ============================================================
    const juce::String& getTypeId() const noexcept { return typeId; }
    const juce::String& getName() const noexcept   { return name; }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept                  { return enabled.load (std::memory_order_relaxed); }

    const std::vector<std::unique_ptr<Parameter>>& getParameters() const noexcept { return parameters; }
    Parameter* findParameter (const juce::String& id) const noexcept;

    /** Smoothed share of the buffer's time this block used (0.01 = 1 %). */
    double getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }

    /** Extra delay the block adds, in samples (0 for most). */
    virtual int getLatencySamples() const noexcept { return 0; }

    //== Off the audio thread, while not playing ===============================
    void prepare (const ProcessSpec& spec);

    //== Audio thread ==========================================================
    /** Processes numChannels channels in place. numSamples <= spec.maxBlockSize. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void reset() noexcept;

protected:
    Parameter& addParameter (Parameter::Spec spec);

    virtual void prepareBlock (const ProcessSpec& spec) = 0;
    virtual void processBlock (float* const* channels, int numChannels, int numSamples) noexcept = 0;
    virtual void resetBlock() noexcept = 0;

private:
    const juce::String typeId, name;
    std::vector<std::unique_ptr<Parameter>> parameters;

    std::atomic<bool> enabled { true };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetAmount { 1.0f };
    bool sleeping = false;                 // fully bypassed: processBlock is skipped entirely
    juce::AudioBuffer<float> dryCopy;      // preallocated for bypass crossfades

    std::atomic<double> cpuLoad { 0.0 };
    double secondsPerSample = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Block)
};

} // namespace encore
