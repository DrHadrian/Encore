// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/PluginBlock.h"
#include "engine/core/MessageThread.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <malloc.h>
#endif

#include <cmath>

namespace encore
{

std::atomic<int> PluginBlock::faultCount { 0 };

namespace
{
    // The plugin this thread is inside right now, for the crash handler.
    thread_local const char* pluginInUse = nullptr;

    std::function<void (PluginBlock&)> beforeClosing;   // message thread

    constexpr int overrunsBeforeSwitchingOff = 3;
    constexpr double maxDrainSeconds = 10.0;
    constexpr float drainedLevel = 1.0e-5f;   // -100 dB

   #if JUCE_WINDOWS
    /** Calls the plugin; false if it crashed. No C++ objects with destructors
        may live in this function (structured exception handling). */
    bool processGuarded (juce::AudioProcessor* plugin, juce::AudioBuffer<float>* buffer, juce::MidiBuffer* midi) noexcept
    {
        __try
        {
            plugin->processBlock (*buffer, *midi);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
                _resetstkoflw();

            return false;
        }
    }
   #else
    // Encore runs on Windows; elsewhere (tests on Linux) a crash is a crash.
    bool processGuarded (juce::AudioProcessor* plugin, juce::AudioBuffer<float>* buffer, juce::MidiBuffer* midi) noexcept
    {
        plugin->processBlock (*buffer, *midi);
        return true;
    }
   #endif

    juce::String parameterId (juce::AudioProcessorParameter& p)
    {
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (&p))
            return hosted->getParameterID();

        return juce::String (p.getParameterIndex());
    }
}

/** Marks the plugin this thread is about to call, for the crash handler. */
class PluginBlock::ScopedCall
{
public:
    explicit ScopedCall (const PluginBlock& block) noexcept : previous (pluginInUse) { pluginInUse = block.crashLabel.c_str(); }
    ~ScopedCall() { pluginInUse = previous; }

private:
    const char* previous;
    JUCE_DECLARE_NON_COPYABLE (ScopedCall)
};

const char* PluginBlock::getPluginInUseOnThisThread() noexcept
{
    return pluginInUse;
}

std::vector<std::pair<juce::String, juce::String>> PluginBlock::parseCrashLabels (const juce::String& labels)
{
    std::vector<std::pair<juce::String, juce::String>> parsed;

    for (const auto& line : juce::StringArray::fromLines (labels))
        if (const auto l = line.trim(); l.containsChar ('\t'))
            parsed.push_back ({ l.upToFirstOccurrenceOf ("\t", false, false), l.fromFirstOccurrenceOf ("\t", false, false) });

    return parsed;
}

//==============================================================================
namespace
{
    /** Which plugin file each loaded plugin block plays, for the crash
        handler. Changed on the message thread; read by the crash handler with
        a try-lock (a crash may happen while it's held). */
    struct LoadedPlugin
    {
        const PluginBlock* block;
        juce::String file;
        juce::String label;
    };

    juce::SpinLock loadedLock;
    std::vector<LoadedPlugin> loadedPlugins;

    /** The .vst3 a path belongs to: a VST3 is usually a folder with its DLL
        inside, sometimes the DLL itself, and either may be reported. */
    juce::String vst3Root (const juce::String& path)
    {
        const auto at = path.indexOfIgnoreCase (".vst3");

        if (at < 0)
            return path;

        const auto end = at + 5;
        return end == path.length() || path[end] == '/' || path[end] == '\\' ? path.substring (0, end) : path;
    }
}

juce::String PluginBlock::getCrashLabelsForModule (const juce::String& modulePath)
{
    const juce::SpinLock::ScopedTryLockType lock (loadedLock);

    if (! lock.isLocked() || modulePath.isEmpty())
        return {};

    juce::StringArray labels;

    const auto root = vst3Root (modulePath);

    for (const auto& p : loadedPlugins)
        if (p.file.isNotEmpty() && vst3Root (p.file).equalsIgnoreCase (root))
            labels.addIfNotAlreadyThere (p.label);

    return labels.joinIntoString ("\n");
}

//==============================================================================
PluginBlock::PluginBlock (const PluginRef& refToUse, std::unique_ptr<juce::AudioPluginInstance> instanceToUse,
                          const juce::String& savedState, const juce::String& blockId)
    : Block (typeId, refToUse.name.isNotEmpty() ? refToUse.name : juce::String ("Plugin")),
      ref (refToUse), instance (std::move (instanceToUse)), state (savedState)
{
    setModelId (blockId);
    crashLabel = (blockId + "\t" + getName()).toStdString();

    if (instance == nullptr)
        return;

    instance->addListener (this);

    {
        const juce::SpinLock::ScopedLockType lock (loadedLock);
        loadedPlugins.push_back ({ this, instance->getPluginDescription().fileOrIdentifier, juce::String (crashLabel) });
    }

    const auto& all = instance->getParameters();
    encoreByIndex.assign ((size_t) all.size(), nullptr);

    // Encore's own on/off (a click-free bypass, stomps) replaces the plugin's bypass.
    const auto* bypass = instance->getBypassParameter();

    for (auto* p : all)
    {
        if (! p->isAutomatable() || p->isMetaParameter() || p == bypass)
            continue;

        Parameter::Spec spec;
        spec.id = parameterId (*p);
        spec.name = p->getName (64);
        spec.range = { 0.0f, 1.0f };
        spec.defaultValue = p->getDefaultValue();
        spec.unit = p->getLabel();
        spec.decimals = 2;
        spec.rampSeconds = 0.0;   // the plugin smooths its own parameters
        spec.textFunction = [this, p] (float v)
        {
            if (getInstance() == nullptr)
                return juce::String (v, 2);

            const ScopedCall call (*this);
            const auto text = p->getText (v, 32);
            const auto label = p->getLabel();
            return label.isEmpty() || text.endsWith (label) ? text : text + " " + label;
        };

        auto& param = addParameter (std::move (spec));
        param.setValue (p->getValue());

        pluginParams.push_back (p);
        encoreParams.push_back (&param);
        lastPushed.push_back (p->getValue());
        encoreByIndex[(size_t) p->getParameterIndex()] = &param;
        p->addListener (this);
    }

    // The guitar goes into the main input only (a sidechain gets silence),
    // and only the main output comes back.
    numIns = instance->getMainBusNumInputChannels();
    numOuts = instance->getMainBusNumOutputChannels();
    numPluginChannels = juce::jmax (instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    latency.store (instance->getLatencySamples(), std::memory_order_relaxed);
    tailSeconds = instance->getTailLengthSeconds();
}

void PluginBlock::setBeforeClosing (std::function<void (PluginBlock&)> fn)
{
    beforeClosing = std::move (fn);
}

PluginBlock::~PluginBlock()
{
    if (instance == nullptr)
        return;

    {
        const juce::SpinLock::ScopedLockType lock (loadedLock);
        loadedPlugins.erase (std::remove_if (loadedPlugins.begin(), loadedPlugins.end(),
                                             [this] (const LoadedPlugin& p) { return p.block == this; }),
                             loadedPlugins.end());
    }

    // Its window (the plugin's editor) goes before the plugin.
    if (beforeClosing != nullptr && juce::MessageManager::existsAndIsCurrentThread())
        beforeClosing (*this);

    for (auto* p : pluginParams)
        p->removeListener (this);

    instance->removeListener (this);

    if (getFault() == Fault::crashed)
    {
        // It may still hold its own locks: closing it could hang Encore.
        // Leave it be until the process ends.
        juce::ignoreUnused (instance.release());
        return;
    }

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        const ScopedCall call (*this);
        instance->releaseResources();
        instance = nullptr;
        return;
    }

    // VST3 plugins are closed on the message thread. If it's gone (Encore is
    // quitting), the plugin goes with the process.
    std::shared_ptr<juce::AudioPluginInstance> closing (instance.release());
    juce::MessageManager::callAsync ([closing] { closing->releaseResources(); });
}

void PluginBlock::setUp (juce::AudioPluginInstance& plugin, const juce::String& savedState)
{
    // Main buses: mono in/out if it can, else mono in/stereo out, else stereo.
    // Extra buses (sidechains) are switched off.
    const auto original = plugin.getBusesLayout();

    if (! original.inputBuses.isEmpty() && ! original.outputBuses.isEmpty())
    {
        using Set = juce::AudioChannelSet;
        const std::pair<Set, Set> wanted[] { { Set::mono(), Set::mono() },
                                             { Set::mono(), Set::stereo() },
                                             { Set::stereo(), Set::stereo() } };

        for (const auto& [in, out] : wanted)
        {
            auto layout = original;
            layout.inputBuses.getReference (0) = in;
            layout.outputBuses.getReference (0) = out;

            for (int i = 1; i < layout.inputBuses.size(); ++i)
                layout.inputBuses.getReference (i) = Set::disabled();

            for (int i = 1; i < layout.outputBuses.size(); ++i)
                layout.outputBuses.getReference (i) = Set::disabled();

            if (plugin.checkBusesLayoutSupported (layout) && plugin.setBusesLayout (layout))
                break;
        }
    }

    // Some plugins (e.g. UAD) refuse all of those or start with their main
    // buses switched off: then turn their main buses on as they are.
    if (plugin.getMainBusNumInputChannels() == 0 || plugin.getMainBusNumOutputChannels() == 0)
    {
        for (const auto isInput : { true, false })
            if (auto* bus = plugin.getBus (isInput, 0); bus != nullptr && ! bus->isEnabled())
                bus->enable (true);
    }

    plugin.setNonRealtime (false);

    if (savedState.isNotEmpty())
    {
        juce::MemoryBlock data;

        if (data.fromBase64Encoding (savedState) && data.getSize() > 0)
            plugin.setStateInformation (data.getData(), (int) data.getSize());
    }
}

//==============================================================================
juce::AudioPluginInstance* PluginBlock::getInstance() const noexcept
{
    return getFault() == Fault::crashed ? nullptr : instance.get();
}

juce::String PluginBlock::getState()
{
    auto* plugin = getInstance();

    if (plugin == nullptr)
        return state;

    if (stateChanged.exchange (false) || ! stateRead)
    {
        juce::MemoryBlock data;
        const ScopedCall call (*this);
        plugin->getStateInformation (data);
        state = data.getSize() > 0 ? data.toBase64Encoding() : juce::String();
        stateRead = true;
    }

    return state;
}

juce::String PluginBlock::getLoadedFileInfo() const
{
    if (instance == nullptr)
        return {};

    juce::StringArray parts { ref.format, ref.maker };

    if (ref.version.isNotEmpty())
        parts.add ("v" + ref.version);

    parts.removeEmptyStrings();
    return parts.joinIntoString (" · ");
}

void PluginBlock::collectGarbage()
{
    // Resets asked for on the audio thread (panic, a patch that finished
    // ringing) happen here: a plugin's reset may allocate.
    if (resetRequested.exchange (false))
        if (auto* plugin = getInstance())
        {
            const ScopedCall call (*this);
            plugin->reset();
        }
}

//==============================================================================
juce::String PluginBlock::getFaultText() const
{
    switch (getFault())
    {
        case Fault::crashed:         return "Crashed and was switched off";
        case Fault::badOutput:       return "Sent broken audio and was switched off";
        case Fault::tooSlow:         return "Took longer than the audio buffer and was switched off";
        case Fault::crashedLastTime: return "Switched off: it crashed Encore last time";
        case Fault::none:            break;
    }

    return {};
}

bool PluginBlock::canTurnBackOn() const noexcept
{
    const auto f = getFault();
    return instance != nullptr && f != Fault::none && f != Fault::crashed;
}

void PluginBlock::turnBackOn()
{
    if (! canTurnBackOn())
        return;

    resetRequested.store (true);
    collectGarbage();   // a clean start
    fault.store (Fault::none);
}

void PluginBlock::markCrashedLastTime() noexcept
{
    if (getFault() == Fault::none)
        fault.store (Fault::crashedLastTime);
}

void PluginBlock::setFault (Fault f) noexcept
{
    fault.store (f);
    faultCount.fetch_add (1, std::memory_order_relaxed);
}

//==============================================================================
int PluginBlock::getLatencySamples() const noexcept
{
    return latency.load (std::memory_order_relaxed);
}

bool PluginBlock::hasTail() const noexcept
{
    // Many plugins report no tail even when they have one: a loaded plugin
    // always rings out, until it's silent (the player detects that).
    return instance != nullptr;
}

void PluginBlock::prepareBlock (const ProcessSpec& spec)
{
    secondsPerSample = 1.0 / spec.sampleRate;
    work.setSize (juce::jmax (1, numPluginChannels), spec.maxBlockSize, false, true, false);
    midi.ensureSize (2048);

    if (instance != nullptr && getFault() != Fault::crashed)
    {
        // VST3 wants this on the message thread.
        pluginPrepared.store (false);

        const auto done = runOnMessageThread ([this, spec]
        {
            const ScopedCall call (*this);
            instance->setRateAndBufferSizeDetails (spec.sampleRate, spec.maxBlockSize);
            instance->prepareToPlay (spec.sampleRate, spec.maxBlockSize);
            latency.store (instance->getLatencySamples(), std::memory_order_relaxed);
        }, keepWaiting);

        pluginPrepared.store (done);
    }

    const auto tail = std::isfinite (tailSeconds) ? juce::jlimit (0.0, maxDrainSeconds, tailSeconds) : maxDrainSeconds;
    maxDrainSamples = (int) (tail * spec.sampleRate) + getLatencySamples() + spec.maxBlockSize;
    drainSamplesLeft = 0;
}

void PluginBlock::resetBlock() noexcept
{
    overrunsInARow = 0;
    drainSamplesLeft = 0;
    resetRequested.store (true);
}

void PluginBlock::wakeBlock() noexcept
{
    // Its tail was played out silently while it was off (idleBlock): carry on.
    overrunsInARow = 0;
}

void PluginBlock::idleBlock (int numSamples) noexcept
{
    if (drainSamplesLeft <= 0 || instance == nullptr || ! pluginPrepared.load (std::memory_order_relaxed)
        || getFault() != Fault::none)
        return;

    work.clear();

    if (! runPlugin (numSamples))
    {
        setFault (Fault::crashed);
        return;
    }

    auto peak = 0.0f;

    for (int ch = 0; ch < numOuts; ++ch)
        peak = juce::jmax (peak, work.getMagnitude (ch, 0, numSamples));

    drainSamplesLeft = (peak < drainedLevel || ! std::isfinite (peak)) ? 0 : drainSamplesLeft - numSamples;
}

void PluginBlock::pushParameters() noexcept
{
    for (size_t i = 0; i < pluginParams.size(); ++i)
    {
        const auto v = encoreParams[i]->getValue();

        if (! juce::exactlyEqual (v, lastPushed[i]))
        {
            lastPushed[i] = v;
            pluginParams[i]->setValue (v);
            stateChanged.store (true, std::memory_order_relaxed);
        }
    }
}

bool PluginBlock::runPlugin (int numSamples) noexcept
{
    // A view of the first numSamples (no allocation: few channels).
    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), numSamples);
    midi.clear();

    const ScopedCall call (*this);
    return processGuarded (instance.get(), &view, &midi);
}

void PluginBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                const ProcessContext&) noexcept
{
    // Not loaded or prepared, or switched off by the guard: the dry signal passes.
    if (instance == nullptr || ! pluginPrepared.load (std::memory_order_relaxed)
        || getFault() != Fault::none || numChannels <= 0)
        return;

    // The guitar into every input channel; output-only channels start silent.
    for (int ch = 0; ch < work.getNumChannels(); ++ch)
    {
        if (ch < numIns)
            work.copyFrom (ch, 0, channels[juce::jmin (ch, numChannels - 1)], numSamples);
        else
            work.clear (ch, 0, numSamples);
    }

    pushParameters();

    const auto start = juce::Time::getHighResolutionTicks();

    if (! runPlugin (numSamples))
    {
        setFault (Fault::crashed);
        return;
    }

    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);

    // Broken audio never leaves the block.
    auto sum = 0.0f;

    for (int ch = 0; ch < numOuts; ++ch)
    {
        const auto* out = work.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
            sum += std::abs (out[i]);
    }

    if (! std::isfinite (sum))
    {
        setFault (Fault::badOutput);
        return;
    }

    if (elapsed > numSamples * secondsPerSample)
    {
        if (++overrunsInARow >= overrunsBeforeSwitchingOff)
        {
            setFault (Fault::tooSlow);
            return;
        }
    }
    else
    {
        overrunsInARow = 0;
    }

    // Back to the chain's channels: a stereo output into mono is averaged.
    if (numOuts <= 0)
        return;

    if (numChannels == 1 && numOuts > 1)
    {
        auto* out = channels[0];
        const auto scale = 1.0f / (float) numOuts;
        juce::FloatVectorOperations::copyWithMultiply (out, work.getReadPointer (0), scale, numSamples);

        for (int ch = 1; ch < numOuts; ++ch)
            juce::FloatVectorOperations::addWithMultiply (out, work.getReadPointer (ch), scale, numSamples);
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (channels[ch], work.getReadPointer (juce::jmin (ch, numOuts - 1)), numSamples);
    }

    // If it's switched off next, its tail plays out silently (idleBlock).
    drainSamplesLeft = maxDrainSamples;
}

//==============================================================================
void PluginBlock::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{
    // Not a settings change by itself: many plugins report meters as
    // (read-only) parameters, many times a second, and reading a big plugin's
    // state on every autosave would stall the message thread. Changes to the
    // parameters that are settings arrive in parameterValueChanged; other
    // changes in audioProcessorChanged.
}

void PluginBlock::audioProcessorChanged (juce::AudioProcessor* processor, const ChangeDetails& details)
{
    if (details.latencyChanged && processor != nullptr)
        latency.store (processor->getLatencySamples(), std::memory_order_relaxed);

    if (details.parameterInfoChanged || details.programChanged || details.nonParameterStateChanged)
        stateChanged.store (true, std::memory_order_relaxed);
}

void PluginBlock::parameterValueChanged (int index, float newValue)
{
    // Moved in the plugin's own window (or by the plugin itself): Encore's
    // parameter follows, so pedals and saving see the same value.
    if (index >= 0 && (size_t) index < encoreByIndex.size())
        if (auto* p = encoreByIndex[(size_t) index])
            p->setValue (newValue);

    stateChanged.store (true, std::memory_order_relaxed);
}

} // namespace encore
