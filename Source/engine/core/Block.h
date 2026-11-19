// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
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

/** What a chain tells each block about the current callback, beyond its own input. */
struct ProcessContext
{
    /** The chain's input (the dry guitar) for this block of samples, mono.
        Lets e.g. a gate after the amp listen to the clean signal. May be null. */
    const float* chainInput = nullptr;
};

/** A block that loads a file (NAM model, cab IR...) shows a "Load" button for it. */
struct FileSlot
{
    juce::String buttonText;    // "Load model..."
    juce::String wildcard;      // "*.nam"
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

    /** The ID of the saved block this was built from (message thread). */
    const juce::String& getModelId() const noexcept         { return modelId; }
    void setModelId (const juce::String& id)                 { modelId = id; }

    /** The file path the saved patch asked for, kept even if loading failed
        (e.g. a USB stick not plugged in), so saving never drops it. */
    const juce::String& getRequestedFile() const noexcept   { return requestedFile; }
    void setRequestedFile (const juce::String& path)         { requestedFile = path; }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept                  { return enabled.load (std::memory_order_relaxed); }

    /** Which stomp switch (1..8) toggles this block, 0 = none. Any thread. */
    void setStomp (int number) noexcept { stomp.store (number, std::memory_order_relaxed); }
    int getStomp() const noexcept       { return stomp.load (std::memory_order_relaxed); }

    const std::vector<std::unique_ptr<Parameter>>& getParameters() const noexcept { return parameters; }
    Parameter* findParameter (const juce::String& id) const noexcept;

    /** Smoothed share of the buffer's time this block used (0.01 = 1 %). */
    double getCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }

    /** Extra delay the block adds, in samples (0 for most). */
    virtual int getLatencySamples() const noexcept { return 0; }

    /** True for blocks whose sound rings on after the input stops (delay,
        reverb). Patch switching lets these ring out ("spill-over"). */
    virtual bool hasTail() const noexcept { return false; }

    //== Files (message thread) ================================================
    /** Non-null if this block loads a file. */
    virtual const FileSlot* getFileSlot() const noexcept { return nullptr; }

    /** Loads in the background and swaps in without a gap; calls onDone on the
        message thread with an empty string on success, or an error message. */
    virtual void loadFileAsync (const juce::File&, std::function<void (juce::String error)> onDone)
    {
        if (onDone != nullptr)
            onDone ("This block doesn't load files");
    }

    /** The file currently in use, if any. */
    virtual juce::File getLoadedFile() const { return {}; }

    /** One line about the loaded file for the UI ("48 kHz, ~9 % CPU"). */
    virtual juce::String getLoadedFileInfo() const { return {}; }

    //== Message thread ========================================================
    /** Frees anything the audio thread has finished with (e.g. a replaced model). */
    virtual void collectGarbage() {}

    //== Off the audio thread, while not playing ===============================
    void prepare (const ProcessSpec& spec);

    /** The spec of the last prepare() (invalid before the first). Message thread. */
    const ProcessSpec& getPreparedSpec() const noexcept { return preparedSpec; }

    //== Audio thread ==========================================================
    /** Processes numChannels channels in place. numSamples <= spec.maxBlockSize. */
    void process (float* const* channels, int numChannels, int numSamples,
                  const ProcessContext& context = {}) noexcept;
    void reset() noexcept;

protected:
    Parameter& addParameter (Parameter::Spec spec);

    virtual void prepareBlock (const ProcessSpec& spec) = 0;
    virtual void processBlock (float* const* channels, int numChannels, int numSamples,
                               const ProcessContext& context) noexcept = 0;
    virtual void resetBlock() noexcept = 0;

    /** Audio thread: the block starts processing again after being fully
        bypassed. Default: a clean state (resetBlock). */
    virtual void wakeBlock() noexcept { resetBlock(); }

    /** Audio thread: called instead of processBlock while fully bypassed
        (e.g. a plugin finishing its tail silently). Default: nothing. */
    virtual void idleBlock (int numSamples) noexcept { juce::ignoreUnused (numSamples); }

private:
    const juce::String typeId, name;
    juce::String modelId, requestedFile;
    std::vector<std::unique_ptr<Parameter>> parameters;

    std::atomic<bool> enabled { true };
    std::atomic<int> stomp { 0 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetAmount { 1.0f };
    bool sleeping = false;                 // fully bypassed: processBlock is skipped entirely
    juce::AudioBuffer<float> dryCopy;      // preallocated for bypass crossfades

    std::atomic<double> cpuLoad { 0.0 };
    double secondsPerSample = 0.0;
    ProcessSpec preparedSpec;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Block)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Block)
};

} // namespace encore
