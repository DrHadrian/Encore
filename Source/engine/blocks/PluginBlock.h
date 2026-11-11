// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <string>

#include "engine/core/Block.h"
#include "model/ShowModel.h"

namespace encore
{

/** A VST3 plugin as a block in a chain.

    The plugin is created (and its saved settings restored) on the message
    thread by the plugin catalog; this block plays it. Without a plugin (not
    installed, blocked, failed to load) the block passes the signal through
    and keeps what the show saved about it, so saving never loses it.

    Encore is mono until routing (milestone 11): the guitar goes to the
    plugin's inputs (both, for a stereo plugin), and a stereo output is
    averaged back to mono.

    Crash guard (decision 0046): a plugin that crashes inside processBlock
    (Windows: caught as a structured exception), outputs NaN/inf, or takes
    longer than the whole buffer three times in a row is switched off at once:
    the dry signal passes and the fault is reported. A crashed plugin is never
    called again, not even to close it (it may hold its own locks): it is
    left in memory until Encore quits.

    Parameters: the plugin's automatable parameters become Encore parameters
    (0..1), so stomps and expression pedals reach them; changes made in the
    plugin's own window come back into them.
*/
class PluginBlock final : public Block,
                          private juce::AudioProcessorListener,
                          private juce::AudioProcessorParameter::Listener
{
public:
    static constexpr const char* typeId = pluginBlockType;

    enum class Fault { none, crashed, badOutput, tooSlow, crashedLastTime };

    /** instance may be null (the plugin couldn't be loaded). savedState is the
        base64 state the show had, already restored into the instance. */
    PluginBlock (const PluginRef& ref, std::unique_ptr<juce::AudioPluginInstance> instance,
                 const juce::String& savedState, const juce::String& blockId);
    ~PluginBlock() override;

    /** Message thread, right after creating the plugin and before it plays:
        mono/stereo buses, the saved state. */
    static void setUp (juce::AudioPluginInstance&, const juce::String& savedState);

    //== Message thread ========================================================
    const PluginRef& getPluginRef() const noexcept { return ref; }

    /** The plugin, or null if it isn't loaded or has crashed (never touch a crashed one). */
    juce::AudioPluginInstance* getInstance() const noexcept;

    bool isLoaded() const noexcept { return instance != nullptr; }

    /** The plugin's settings as the show saves them (base64). Asks the plugin
        only if something changed since last time, else returns the last one. */
    juce::String getState();

    juce::String getLoadedFileInfo() const override;
    void collectGarbage() override;

    /** Loader threads waiting for the message thread (to prepare the plugin)
        give up when this returns false. */
    void setKeepWaiting (std::function<bool()> fn) { keepWaiting = std::move (fn); }

    //== Faults (any thread) ===================================================
    Fault getFault() const noexcept { return fault.load (std::memory_order_relaxed); }

    /** "Crashed and was switched off" etc., empty if fine. */
    juce::String getFaultText() const;

    /** Message thread: tries a plugin that misbehaved (bad output, too slow,
        crashed last time) again. A crashed plugin can't be: rebuild the patch. */
    bool canTurnBackOn() const noexcept;
    void turnBackOn();

    /** Message thread: start switched off (it crashed Encore last time). */
    void markCrashedLastTime() noexcept;

    /** Goes up by one whenever any plugin block faults, so the UI notices
        without polling every block. */
    static int getFaultCount() noexcept { return faultCount.load (std::memory_order_relaxed); }

    /** For the crash handler: "<block id>\t<plugin name>" of the plugin this
        thread is calling right now, or null. Parse it with parseCrashLabel. */
    static const char* getPluginInUseOnThisThread() noexcept;

    /** Splits a crash label into the block ID and the plugin's name. */
    static std::pair<juce::String, juce::String> parseCrashLabel (const juce::String&);

    //== Block =================================================================
    int getLatencySamples() const noexcept override;
    bool hasTail() const noexcept override;

private:
    class ScopedCall;

    void prepareBlock (const ProcessSpec& spec) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext& context) noexcept override;
    void resetBlock() noexcept override;
    void wakeBlock() noexcept override;
    void idleBlock (int numSamples) noexcept override;

    bool runPlugin (int numSamples) noexcept;
    void setFault (Fault) noexcept;
    void pushParameters() noexcept;

    // Listeners (any thread).
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void parameterValueChanged (int index, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    const PluginRef ref;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String state;                                   // the last state read (or the saved one)
    std::string crashLabel;

    // The plugin's parameters and the Encore parameter for each.
    std::vector<juce::AudioProcessorParameter*> pluginParams;
    std::vector<Parameter*> encoreParams;
    std::vector<float> lastPushed;                        // audio thread
    std::vector<Parameter*> encoreByIndex;                // by the plugin's parameter index (null: not exposed)

    juce::AudioBuffer<float> work;                        // the plugin's channels
    juce::MidiBuffer midi;
    int numIns = 0, numOuts = 0;
    double secondsPerSample = 0.0;
    int overrunsInARow = 0;
    int drainSamplesLeft = 0, maxDrainSamples = 0;        // feeding silence while bypassed

    std::atomic<int> latency { 0 };
    double tailSeconds = 0.0;
    bool stateRead = false;                               // message thread

    std::atomic<Fault> fault { Fault::none };
    std::atomic<bool> stateChanged { false }, resetRequested { false }, pluginPrepared { false };
    std::function<bool()> keepWaiting;

    static std::atomic<int> faultCount;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBlock)
};

} // namespace encore
