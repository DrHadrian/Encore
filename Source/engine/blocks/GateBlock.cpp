// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/GateBlock.h"
#include "engine/dsp/FilterDesign.h"

namespace encore
{

GateBlock::GateBlock (juce::String typeIdToUse, juce::String displayName)
    : Block (std::move (typeIdToUse), std::move (displayName))
{
}

GateBlock::~GateBlock() = default;

int GateBlock::getLatencySamples() const noexcept
{
    return hasLookAhead() ? getLookAheadFromParameters() : 0;
}

void GateBlock::setKeyTap (const float* const* tapChannels, int numTapChannels, const juce::String& problem)
{
    keyTap = tapChannels;
    keyTapChannels = tapChannels != nullptr ? juce::jlimit (1, 2, numTapChannels) : 0;
    keyTapProblem = problem;
    keyBlockNamed = tapChannels != nullptr || problem.isNotEmpty();
}

void GateBlock::setKeyInputSlot (int slot) noexcept
{
    keyInputSlot.store (slot, std::memory_order_relaxed);
    keyInputNamed.store (true, std::memory_order_relaxed);
}

juce::String GateBlock::getKeyInfo() const
{
    switch (getKeyState())
    {
        case KeyState::inUse:          return {};
        case KeyState::inputNotHere:   return "Key: " + getRef ("keyInput") + " isn't on this computer";
        case KeyState::noInputPicked:  return "Key: no input picked, so it listens to its own input";
        case KeyState::blockNotUsable: return keyTapProblem.isNotEmpty() ? keyTapProblem : juce::String ("Key: that block isn't before this gate");
        case KeyState::noBlockPicked:  return "Key: no block picked, so it listens to its own input";
    }

    return {};
}

void GateBlock::setLearnedFloorDb (float db) noexcept
{
    for (auto* engine : { &current, &old })
        for (auto& lane : engine->lanes)
            lane.adaptive.setFloorDb (db);

    floorNow.store (current.lanes[0].adaptive.getFloorDb(), std::memory_order_relaxed);
}

//==============================================================================
void GateBlock::Engine::prepare (double rate)
{
    for (auto& lane : lanes)
    {
        lane.detector.prepare (rate);
        lane.envelope.prepare (rate);
        lane.adaptive.prepare (rate);
    }

    highPassHz = lowPassHz = -1.0f;
    configure (setup, rate);
    reset();
}

void GateBlock::Engine::configure (const GateSetup& newSetup, double rate) noexcept
{
    const auto first = highPassHz < 0.0f;

    if (! first && newSetup == setup)
        return;

    setup = newSetup;

    dsp::GateEnvelopeSettings envelope;
    envelope.thresholdDb = setup.thresholdDb;
    envelope.hysteresisDb = setup.hysteresisDb;
    envelope.rangeDb = setup.rangeDb;
    envelope.attackMs = setup.attackMs;
    envelope.holdMs = setup.holdMs;
    envelope.releaseMs = setup.releaseMs;
    envelope.ratio = setup.ratio;
    envelope.mode = setup.way == GateWay::expander ? dsp::GateMode::expander
                  : setup.way == GateWay::ducker   ? dsp::GateMode::ducker
                                                   : dsp::GateMode::gate;

    dsp::AdaptiveGateSettings adaptive;
    adaptive.offsetDb = setup.smartOffsetDb;
    adaptive.baseReleaseMs = setup.releaseMs;
    adaptive.baseHoldMs = setup.holdMs;
    adaptive.attackMs = setup.attackMs;
    adaptive.hysteresisDb = setup.hysteresisDb;
    adaptive.rangeDb = setup.rangeDb;
    adaptive.mode = setup.smartExpander ? dsp::GateMode::expander : dsp::GateMode::gate;
    adaptive.ratio = setup.ratio;

    for (auto& lane : lanes)
    {
        lane.detector.setMode (setup.detector);
        lane.detector.setTimeMs (setup.detectorMs);

        if (setup.way == GateWay::smart)
        {
            if (! (adaptive.offsetDb == lane.adaptive.getSettings().offsetDb
                   && adaptive.baseReleaseMs == lane.adaptive.getSettings().baseReleaseMs
                   && adaptive.baseHoldMs == lane.adaptive.getSettings().baseHoldMs
                   && adaptive.attackMs == lane.adaptive.getSettings().attackMs
                   && adaptive.hysteresisDb == lane.adaptive.getSettings().hysteresisDb
                   && adaptive.rangeDb == lane.adaptive.getSettings().rangeDb
                   && adaptive.mode == lane.adaptive.getSettings().mode
                   && adaptive.ratio == lane.adaptive.getSettings().ratio))
                lane.adaptive.setSettings (adaptive);
        }
        else
        {
            lane.envelope.setSettings (envelope);
        }
    }

    // The key filter: 12 dB/oct Butterworth sections, designed only when moved.
    const auto nyquistSafe = (float) (rate * 0.45);

    if (setup.keyHighPassHz != highPassHz)
    {
        highPassHz = setup.keyHighPassHz;

        if (highPassHz > 0.0f)
            highPassCoefficients = dsp::SvfCoefficients::fromDesign (
                dsp::design::highPass (rate, juce::jmin (highPassHz, nyquistSafe), 0.7071067811865476));
    }

    if (setup.keyLowPassHz != lowPassHz)
    {
        lowPassHz = setup.keyLowPassHz;

        if (lowPassHz > 0.0f)
            lowPassCoefficients = dsp::SvfCoefficients::fromDesign (
                dsp::design::lowPass (rate, juce::jmin (lowPassHz, nyquistSafe), 0.7071067811865476));
    }
}

void GateBlock::Engine::reset() noexcept
{
    for (auto& lane : lanes)
    {
        lane.detector.reset();
        lane.envelope.reset (true);
        lane.adaptive.reset (true);
    }

    highPass.reset();
    lowPass.reset();
}

float GateBlock::Engine::filter (float x, int channel) noexcept
{
    auto y = (double) x;

    if (highPassHz > 0.0f)
        y = highPass.tick (y, channel, highPassCoefficients);

    if (lowPassHz > 0.0f)
        y = lowPass.tick (y, channel, lowPassCoefficients);

    return (float) y;
}

void GateBlock::Engine::run (const float* const* key, int keyChannels, int numSamples, int numChannels,
                             float* const* gainsOut, float* const* filtered, bool tunerOpen) noexcept
{
    const auto smart = setup.way == GateWay::smart;
    const auto lanesUsed = setup.stereoLinked ? 1 : juce::jlimit (1, 2, numChannels);
    keyChannels = juce::jlimit (1, 2, keyChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        float k[2] = { filter (key[0][i], 0), 0.0f };
        k[1] = keyChannels > 1 ? filter (key[1][i], 1) : k[0];

        if (filtered != nullptr)
        {
            filtered[0][i] = k[0];
            filtered[1][i] = k[1];
        }

        for (int l = 0; l < lanesUsed; ++l)
        {
            auto& lane = lanes[(size_t) l];
            const auto x = lanesUsed == 1 ? juce::jmax (std::abs (k[0]), std::abs (k[1])) : k[l];
            const auto level = lane.detector.process (x);
            gainsOut[l][i] = smart ? lane.adaptive.process (level, tunerOpen) : lane.envelope.process (level);
        }
    }
}

//==============================================================================
void GateBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    current.prepare (sampleRate);
    old = current;
    fadeLength = juce::jmax (1, juce::roundToInt (switchFadeSeconds * sampleRate));
    fadePosition = fadeLength;
    listenStep = 1.0f / (float) juce::jmax (1, juce::roundToInt (listenFadeSeconds * sampleRate));
    lookAhead.prepare (sampleRate);

    for (auto* buffer : { &keyCopy, &filteredKey, &gains, &oldGains })
        buffer->setSize (2, spec.maxBlockSize, false, true, false);
}

void GateBlock::resetBlock() noexcept
{
    current.reset();
    fadePosition = fadeLength;
    lookAhead.setTimeSamplesNow (getLatencySamples());
    lookAhead.reset();
    listenMix = keyListen.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    publishState();
}

void GateBlock::publishState() noexcept
{
    const auto lanesUsed = current.setup.stereoLinked ? 1 : 2;
    auto open = false, frozen = false;
    auto reduction = 0.0f, floor = -200.0f;

    for (int l = 0; l < lanesUsed; ++l)
    {
        const auto& lane = current.lanes[(size_t) l];

        if (current.setup.way == GateWay::smart)
        {
            open = open || lane.adaptive.isOpen();
            reduction = juce::jmax (reduction, lane.adaptive.getReductionDb());
            floor = juce::jmax (floor, lane.adaptive.getFloorDb());
            frozen = frozen || lane.adaptive.isFrozen();
        }
        else
        {
            open = open || lane.envelope.isOpen();
            reduction = juce::jmax (reduction, lane.envelope.getReductionDb());
        }
    }

    openNow.store (open, std::memory_order_relaxed);
    reductionNow.store (reduction, std::memory_order_relaxed);
    frozenNow.store (frozen, std::memory_order_relaxed);

    if (current.setup.way == GateWay::smart)
        floorNow.store (floor, std::memory_order_relaxed);
}

void GateBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                              const ProcessContext& context) noexcept
{
    numChannels = juce::jlimit (1, 2, numChannels);
    const auto setup = readSetup (numSamples);

    // A switch of way, detector, key or stereo: keep the old engine beside
    // the new one for 10 ms. The new one starts where the old one stood
    // (open or closed), so a closed gate doesn't burst open.
    if (setup.switchesFrom (current.setup) && fadePosition >= fadeLength)
    {
        old = current;
        fadePosition = 0;
        const auto wasOpen = isOpen();
        current.configure (setup, sampleRate);

        for (auto& lane : current.lanes)
        {
            lane.envelope.reset (wasOpen);
            lane.adaptive.reset (wasOpen);
        }
    }
    else if (! setup.switchesFrom (current.setup))
    {
        current.configure (setup, sampleRate);
    }

    // The key, copied before the look-ahead delays the channels in place.
    // One that can't be reached falls back to the gate's own input.
    const float* key[2] = { channels[0], channels[numChannels > 1 ? 1 : 0] };
    auto keyChannels = numChannels;
    auto keyState = KeyState::inUse;

    switch (setup.key)
    {
        case GateKey::input:
            break;

        case GateKey::guitar:
            if (context.chainInput != nullptr)
            {
                key[0] = key[1] = context.chainInput;
                keyChannels = 1;
            }
            break;

        case GateKey::audioInput:
            if (const auto* in = context.input (keyInputSlot.load (std::memory_order_relaxed)))
            {
                key[0] = key[1] = in;
                keyChannels = 1;
            }
            else
            {
                keyState = keyInputNamed.load (std::memory_order_relaxed) ? KeyState::inputNotHere : KeyState::noInputPicked;
            }
            break;

        case GateKey::block:
            if (keyTap != nullptr)
            {
                key[0] = keyTap[0];
                key[1] = keyTap[keyTapChannels > 1 ? 1 : 0];
                keyChannels = keyTapChannels;
            }
            else
            {
                keyState = keyBlockNamed ? KeyState::blockNotUsable : KeyState::noBlockPicked;
            }
            break;
    }

    keyStateNow.store ((int) keyState, std::memory_order_relaxed);

    for (int c = 0; c < keyChannels; ++c)
        keyCopy.copyFrom (c, 0, key[c], numSamples);

    const float* keys[2] = { keyCopy.getReadPointer (0), keyCopy.getReadPointer (keyChannels > 1 ? 1 : 0) };

    if (hasLookAhead())
    {
        lookAhead.setTimeSamples (setup.lookAheadSamples);
        lookAhead.process (channels, numChannels, numSamples);
    }

    // The gains: per sample, from the key.
    const auto tunerOpen = context.tunerOpen;
    float* g[2] = { gains.getWritePointer (0), gains.getWritePointer (1) };
    float* f[2] = { filteredKey.getWritePointer (0), filteredKey.getWritePointer (1) };
    current.run (keys, keyChannels, numSamples, numChannels, g, f, tunerOpen);

    const auto lanesUsed = setup.stereoLinked ? 1 : numChannels;

    if (fadePosition < fadeLength)
    {
        float* og[2] = { oldGains.getWritePointer (0), oldGains.getWritePointer (1) };
        old.run (keys, keyChannels, numSamples, numChannels, og, nullptr, tunerOpen);
        const auto oldLanes = old.setup.stereoLinked ? 1 : numChannels;
        const auto start = fadePosition;

        // The blend is per channel (linked to independent, or back, too).
        if (lanesUsed == 1 && numChannels > 1)
            juce::FloatVectorOperations::copy (g[1], g[0], numSamples);

        for (int c = 0; c < numChannels; ++c)
        {
            auto* out = g[c];
            const auto* oldGain = og[juce::jmin (c, oldLanes - 1)];
            auto p = start;

            for (int i = 0; i < numSamples && p < fadeLength; ++i, ++p)
            {
                const auto t = (float) (p + 1) / (float) fadeLength;
                const auto w = t * t * (3.0f - 2.0f * t);
                out[i] = oldGain[i] + (out[i] - oldGain[i]) * w;
            }
        }

        fadePosition = juce::jmin (fadeLength, start + numSamples);

        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::multiply (channels[c], g[c], numSamples);
    }
    else
    {
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::multiply (channels[c], g[juce::jmin (c, lanesUsed - 1)], numSamples);
    }

    // Key listen: the filtered key on every channel, crossfaded.
    const auto listenTarget = keyListen.load (std::memory_order_relaxed) ? 1.0f : 0.0f;

    if (listenMix > 0.0f || listenTarget > 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            listenMix = listenTarget > listenMix ? juce::jmin (1.0f, listenMix + listenStep)
                                                 : juce::jmax (0.0f, listenMix - listenStep);
            const auto w = listenMix * listenMix * (3.0f - 2.0f * listenMix);

            for (int c = 0; c < numChannels; ++c)
            {
                const auto heard = f[keyChannels > 1 ? c : 0][i];
                channels[c][i] += (heard - channels[c][i]) * w;
            }
        }
    }

    publishState();
}

} // namespace encore
