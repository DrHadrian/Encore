// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <algorithm>
#include <cmath>

namespace encore::dsp
{

/** How a detector measures level (the Encore Engine's DSP, decision 0062). */
enum class DetectorMode
{
    peak,   // instant rise, falls with the detector time: reacts to every pick attack
    rms     // the signal's power averaged over the detector time: follows loudness
};

/** A level detector, sample by sample, for gates, compressors and dynamic
    EQ bands.

    Peak: the level jumps to each new peak and then falls exponentially, by a
    factor of e every detector time (1-50 ms; 10 ms is today's gate).
    RMS: the square root of the signal's power, averaged with the same time
    constant. A sine of peak A reads A in Peak and A / sqrt(2) (3 dB lower) in
    RMS, as on any RMS meter.

    The level is linear amplitude (1 = full scale). State in double.
    prepare() and setters: any thread before playing, or the audio thread
    (no allocation). process(): audio thread, no allocation, no locks.
*/
class LevelDetector
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficient();
        reset();
    }

    void setMode (DetectorMode newMode) noexcept       { mode = newMode; }
    DetectorMode getMode() const noexcept              { return mode; }

    void setTimeMs (double milliseconds) noexcept
    {
        timeMs = std::clamp (milliseconds, 0.1, 1000.0);
        updateCoefficient();
    }

    void reset() noexcept                              { state = 0.0; }

    /** One sample in (a channel, or the max of linked channels): the level. */
    float process (float x) noexcept
    {
        const auto magnitude = std::abs ((double) x);

        if (mode == DetectorMode::peak)
        {
            state = std::max (magnitude, state * coefficient);
            return (float) state;
        }

        state = magnitude * magnitude + (state - magnitude * magnitude) * coefficient;
        return (float) std::sqrt (state);
    }

    /** The current level, without a new sample. */
    float getLevel() const noexcept
    {
        return (float) (mode == DetectorMode::peak ? state : std::sqrt (state));
    }

private:
    void updateCoefficient() noexcept
    {
        coefficient = std::exp (-1000.0 / (timeMs * sampleRate));
    }

    DetectorMode mode = DetectorMode::peak;
    double sampleRate = 48000.0, timeMs = 10.0, coefficient = 0.0, state = 0.0;
};

/** What a gate envelope is set to. */
struct GateEnvelopeSettings
{
    float thresholdDb = -60.0f;   // opens at or above this level
    float hysteresisDb = 6.0f;    // closes only below threshold - hysteresis (0..20 dB)
    float rangeDb = -90.0f;       // the gain when closed (-90..0 dB)
    float attackMs = 0.5f;        // closed -> fully open, linear in amplitude
    float holdMs = 20.0f;         // stays open this long after the level falls below the close level
    float releaseMs = 80.0f;      // fully open -> closed, linear in dB
};

/** A gate's open/close logic and gain, sample-accurate.

    - Opens when the level reaches the threshold; the gain then rises
      linearly to 1 within the attack time.
    - Closes when the level stays below threshold - hysteresis for the hold
      time; the gain then falls to the range, linearly in dB, within the
      release time. A level back at the threshold during the release opens
      it again from where the gain is.
    - Hysteresis keeps a note that decays slowly through the threshold from
      flickering open and shut ("chatter").

    Feed it a LevelDetector's output. Threads: setSettings() and process()
    on the audio thread (no allocation, no locks); prepare() anywhere before.
*/
class GateEnvelope
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        setSettings (settings);
        reset (true);
    }

    void setSettings (const GateEnvelopeSettings& newSettings) noexcept
    {
        settings = newSettings;
        openLevel = dbToGain (settings.thresholdDb);
        closeLevel = dbToGain (settings.thresholdDb - std::clamp (settings.hysteresisDb, 0.0f, 20.0f));

        const auto range = std::clamp (settings.rangeDb, -90.0f, 0.0f);
        floorGain = range <= -90.0f ? 0.0f : dbToGain (range);

        const auto attackSamples = std::max (1.0, settings.attackMs * 0.001 * sampleRate);
        attackStep = (1.0f - floorGain) / (float) attackSamples;

        holdSamples = (int) std::lround (std::max (0.0f, settings.holdMs) * 0.001 * sampleRate);

        // -90 dB counts as silence: release falls 90 dB in the release time, then to 0.
        const auto releaseSamples = std::max (1.0, settings.releaseMs * 0.001 * sampleRate);
        releaseFactor = (float) std::pow (10.0, std::min (range, -0.001f) / 20.0 / releaseSamples);
        silenceGain = dbToGain (-90.0f);
    }

    const GateEnvelopeSettings& getSettings() const noexcept { return settings; }

    /** Open (a block switched on mid-note must not mute it) or closed. */
    void reset (bool startOpen) noexcept
    {
        isOpenNow = startOpen;
        gain = startOpen ? 1.0f : floorGain;
        holdCounter = holdSamples;
    }

    /** One level in, the gain out (floorGain..1). */
    float process (float level) noexcept
    {
        if (level >= openLevel)
        {
            isOpenNow = true;
            holdCounter = holdSamples;
        }
        else if (isOpenNow && level < closeLevel)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                isOpenNow = false;
        }

        if (isOpenNow)
            gain = std::min (1.0f, gain + attackStep);
        else if (gain > floorGain)
            gain = (gain * releaseFactor < std::max (floorGain, silenceGain)) ? floorGain : gain * releaseFactor;

        return gain;
    }

    bool isOpen() const noexcept       { return isOpenNow; }
    float getGain() const noexcept     { return gain; }

private:
    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    GateEnvelopeSettings settings;
    double sampleRate = 48000.0;
    float openLevel = 0.001f, closeLevel = 0.0005f, floorGain = 0.0f, silenceGain = 0.0f;
    float attackStep = 1.0f, releaseFactor = 0.99f, gain = 1.0f;
    int holdSamples = 0, holdCounter = 0;
    bool isOpenNow = true;
};

} // namespace encore::dsp
