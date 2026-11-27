// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/dsp/Detector.h"

#include <array>
#include <atomic>

namespace encore::dsp
{

/** What the adaptive ("Smart") gate is set to: the Pedal Gate "S2"'s two
    knobs and switch, or the Studio Gate "X4"'s Smart mode (GATE-PLAN 13, 23). */
struct AdaptiveGateSettings
{
    float offsetDb = 9.0f;          // the threshold: this far above the learned noise floor
    float baseReleaseMs = 87.0f;    // the release before the note's decay adjusts it
    float baseHoldMs = 20.0f;       // the hold before the chatter guard lengthens it
    float attackMs = 0.3f;          // the short linear fade-in that keeps the pick attack
    float hysteresisDb = 6.0f;
    float rangeDb = -90.0f;         // Hard: -90 (mutes); Soft: -15
    GateMode mode = GateMode::gate; // gate (Hard) or expander (Soft)
    float ratio = 2.0f;             // the expander's (Soft: 1:2)
    bool chatterGuard = true;       // off only to measure what it does (tests)
};

/** The adaptive gate shared by the S2 and the X4's Smart mode, so they
    behave the same (the Encore Engine's DSP; B2, GATE-PLAN 13 and 23).

    - The noise floor follows the noise. It learns only from stretches that
      look like noise, not playing: at least 500 ms with no pick attack (no
      rise of 6 dB within 10 ms) and a level steady within +-3 dB. The floor
      moves toward that stretch's level by at most 6 dB per second and stays
      between -96 and -40 dBFS; before anything is learned it is -70 dBFS.
      The threshold is the floor plus the offset.
    - Freeze: nothing is learned while the level is below -85 dBFS (cable
      out, guitar volume at 0) or while the tuner is open (any tuner, muting
      or not: tuning notes are never noise; Adrian, 27 Nov).
    - The release follows the note: the level's decay over the last 100 ms
      the gate was open sets it. Slower than 20 dB/s (a held chord) stretches
      it up to 2.5x the base; faster than 200 dB/s (a palm-mute) shortens it
      down to 0.25x; in between, log-interpolated.
    - Chatter guard: opening again within 50 ms of closing doubles the next
      hold, up to 200 ms; without another reopen it falls back to the base
      hold over 1 s.
    - Keeps the pick attack: it opens on the peak detector, and the gain
      rises in a 0.3 ms linear fade.

    The learning runs on a decimated grid (about every 0.33 ms), so it costs
    a log10 and a few compares per step, not per sample.

    Threads: prepare() off the audio thread. process(), processWithDecision()
    and reset() on the audio thread: no allocation, no locks. The floor and
    "frozen" can be read from any thread; setFloorDb() is for before playing
    (a rebuilt patch starting from the old one's floor) or the audio thread.
*/
class AdaptiveGate
{
public:
    // Proposed values, as named constants; tuned on the rig in U2.
    static constexpr float initialFloorDb = -70.0f;
    static constexpr float minFloorDb = -96.0f, maxFloorDb = -40.0f;
    static constexpr float freezeBelowDb = -85.0f;
    static constexpr double learnAfterSeconds = 0.5;
    static constexpr float steadyWithinDb = 3.0f;
    static constexpr float attackRiseDb = 6.0f;
    static constexpr double attackWindowSeconds = 0.010;
    static constexpr float floorSlewDbPerSecond = 6.0f;
    static constexpr double decayWindowSeconds = 0.100;
    static constexpr float slowDecayDbPerSecond = 20.0f, fastDecayDbPerSecond = 200.0f;
    static constexpr float slowReleaseFactor = 2.5f, fastReleaseFactor = 0.25f;
    static constexpr double chatterWindowSeconds = 0.050;
    static constexpr float maxHoldMs = 200.0f;
    static constexpr double holdRecoverySeconds = 1.0;
    static constexpr double stepSeconds = 1.0 / 3000.0;
    static constexpr double smoothingSeconds = 0.050;   // the level the steadiness test reads

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        stepSamples = juce::jmax (1, (int) std::lround (sampleRate * stepSeconds));
        const auto stepTime = (double) stepSamples / sampleRate;
        attackSlots = juce::jlimit (1, (int) attackRing.size(), (int) std::ceil (attackWindowSeconds / stepTime));
        decaySlots = juce::jlimit (2, (int) decayRing.size(), (int) std::lround (decayWindowSeconds / stepTime));
        learnAfterSteps = (int) std::lround (learnAfterSeconds / stepTime);
        chatterSamples = (int) std::lround (chatterWindowSeconds * sampleRate);
        slewPerStep = floorSlewDbPerSecond * (float) stepTime;
        smoothing = (float) std::exp (-stepTime / smoothingSeconds);
        holdRecoveryPerStep = (float) (stepTime / holdRecoverySeconds);
        envelope.prepare (sampleRate);
        applySettings();
        reset (true);
    }

    void setSettings (const AdaptiveGateSettings& newSettings) noexcept
    {
        settings = newSettings;
        applySettings();
    }

    const AdaptiveGateSettings& getSettings() const noexcept { return settings; }

    /** Open (switched on mid-note) or closed. Keeps the learned floor. */
    void reset (bool startOpen) noexcept
    {
        envelope.reset (startOpen);
        wasOpen = envelope.isOpen();
        stepCounter = 0;
        attackRing.fill (-200.0f);
        decayRing.fill (-200.0f);
        ringIndex = decayIndex = 0;
        smoothedDb = floorDb;
        restartStretch (floorDb);
        sinceClose = 1 << 30;
        holdBoost = 0.0f;
        peakHoldMs = settings.baseHoldMs;
        releaseFactorNow = 1.0f;
    }

    /** One sample: the peak detector's level and whether the tuner is open;
        the gain out. */
    float process (float level, bool tunerOpen) noexcept
    {
        learn (level, tunerOpen);
        const auto g = envelope.process (level);
        afterDecision();
        return g;
    }

    /** A follower's sample (linking, B2): the leader decided. Learns nothing
        (the leader's floor is shared); its own release and range. */
    float processWithDecision (bool open, float level) noexcept
    {
        trackDecay (level);
        const auto g = envelope.processWithDecision (open, level);
        afterDecision();
        return g;
    }

    bool isOpen() const noexcept          { return envelope.isOpen(); }
    float getGain() const noexcept        { return envelope.getGain(); }
    float getReductionDb() const noexcept { return envelope.getReductionDb(); }

    /** The learned noise floor and the threshold it gives, dBFS. Any thread. */
    float getFloorDb() const noexcept     { return floorForUi.load (std::memory_order_relaxed); }
    float getThresholdDb() const noexcept { return thresholdDb; }

    /** True while learning is frozen (near-silence or the tuner). Any thread. */
    bool isFrozen() const noexcept        { return frozenForUi.load (std::memory_order_relaxed); }

    /** Starts from a floor learned before (a rebuilt patch). */
    void setFloorDb (float db) noexcept
    {
        floorDb = juce::jlimit (minFloorDb, maxFloorDb, db);
        floorForUi.store (floorDb, std::memory_order_relaxed);
        applySettings();
    }

    /** For a follower: the leader's threshold (its floor plus its offset). */
    void setSharedThresholdDb (float db) noexcept
    {
        if (std::abs (db - thresholdDb) > 0.01f)
        {
            thresholdDb = db;
            applyEnvelope();
        }
    }

    /** The release in effect, ms (the base times what the note's decay says). */
    float getReleaseMs() const noexcept   { return settings.baseReleaseMs * releaseFactorNow; }
    float getHoldMs() const noexcept      { return currentHoldMs(); }

private:
    void applySettings() noexcept
    {
        thresholdDb = floorDb + juce::jlimit (0.0f, 60.0f, settings.offsetDb);
        applyEnvelope();
    }

    void applyEnvelope() noexcept
    {
        GateEnvelopeSettings s;
        s.mode = settings.mode;
        s.thresholdDb = thresholdDb;
        s.hysteresisDb = settings.hysteresisDb;
        s.rangeDb = settings.rangeDb;
        s.attackMs = settings.attackMs;
        s.holdMs = currentHoldMs();
        s.releaseMs = settings.baseReleaseMs * releaseFactorNow;
        s.ratio = settings.ratio;
        envelope.setSettings (s);
    }

    float currentHoldMs() const noexcept
    {
        return settings.baseHoldMs + (peakHoldMs - settings.baseHoldMs) * holdBoost;
    }

    void restartStretch (float db) noexcept
    {
        stretchSteps = 0;
        stretchMin = stretchMax = db;
        stretchSum = 0.0;
    }

    static float toDb (float level) noexcept { return level > 1.0e-10f ? 20.0f * std::log10 (level) : -200.0f; }

    /** The decimated grid: noise learning and the note's decay. */
    void learn (float level, bool tunerOpen) noexcept
    {
        if (++stepCounter < stepSamples)
            return;

        stepCounter = 0;
        const auto db = toDb (level);
        pushDecay (db);

        // A pick attack: a rise of 6 dB on the lowest level of the last 10 ms.
        auto lowest = db;

        for (int i = 0; i < attackSlots; ++i)
            lowest = juce::jmin (lowest, attackRing[(size_t) i]);

        attackRing[(size_t) ringIndex] = db;
        ringIndex = ringIndex + 1 < attackSlots ? ringIndex + 1 : 0;

        smoothedDb = db + (smoothedDb - db) * smoothing;
        const auto frozen = tunerOpen || db < freezeBelowDb;
        frozenForUi.store (frozen, std::memory_order_relaxed);

        // The chatter guard's extra hold fades back to the base over 1 s.
        if (holdBoost > 0.0f)
        {
            holdBoost = juce::jmax (0.0f, holdBoost - holdRecoveryPerStep);
            applyEnvelope();
        }

        if (frozen || db - lowest >= attackRiseDb)
        {
            restartStretch (smoothedDb);
            return;
        }

        stretchMin = juce::jmin (stretchMin, smoothedDb);
        stretchMax = juce::jmax (stretchMax, smoothedDb);

        if (stretchMax - stretchMin > 2.0f * steadyWithinDb)
        {
            restartStretch (smoothedDb);
            return;
        }

        ++stretchSteps;
        stretchSum += smoothedDb;

        if (stretchSteps < learnAfterSteps)
            return;

        // Noise: move toward the stretch's level, at most 6 dB per second.
        const auto stretchLevel = (float) (stretchSum / stretchSteps);
        const auto moved = juce::jlimit (minFloorDb, maxFloorDb,
                                         floorDb + juce::jlimit (-slewPerStep, slewPerStep, stretchLevel - floorDb));

        if (moved != floorDb)
        {
            floorDb = moved;
            floorForUi.store (floorDb, std::memory_order_relaxed);
            applySettings();
        }
    }

    /** A follower has no learning grid of its own: its decay ring still runs. */
    void trackDecay (float level) noexcept
    {
        if (++stepCounter < stepSamples)
            return;

        stepCounter = 0;
        pushDecay (toDb (level));
    }

    void pushDecay (float db) noexcept
    {
        decayRing[(size_t) decayIndex] = db;
        decayIndex = decayIndex + 1 < decaySlots ? decayIndex + 1 : 0;
    }

    /** After each sample's decision: the release follows the note while it's
        open; the chatter guard counts the reopenings. */
    void afterDecision() noexcept
    {
        const auto open = envelope.isOpen();

        if (open != wasOpen)
        {
            if (open && settings.chatterGuard && settings.mode != GateMode::expander && sinceClose < chatterSamples)
            {
                peakHoldMs = juce::jmin (maxHoldMs, juce::jmax (currentHoldMs(), settings.baseHoldMs) * 2.0f);
                holdBoost = 1.0f;
                applyEnvelope();
            }

            if (! open)
                sinceClose = 0;

            wasOpen = open;
        }
        else if (! open && sinceClose < (1 << 30))
        {
            ++sinceClose;
        }

        // While open, re-read the decay every step, so the release in effect
        // when it closes is the one the last 100 ms asked for.
        if (open && stepCounter == 0)
        {
            const auto newest = decayRing[(size_t) (decayIndex == 0 ? decaySlots - 1 : decayIndex - 1)];
            const auto oldest = decayRing[(size_t) decayIndex];

            if (oldest > -199.0f)
            {
                const auto window = (float) ((decaySlots - 1) * stepSamples / sampleRate);
                const auto dbPerSecond = juce::jmax (1.0e-3f, (oldest - newest) / window);
                const auto factor = releaseFactorFor (dbPerSecond);

                if (std::abs (factor - releaseFactorNow) > 0.01f)
                {
                    releaseFactorNow = factor;
                    applyEnvelope();
                }
            }
        }
    }

public:
    /** The release factor for a decay rate (dB/s): 2.5 slow, 0.25 fast. */
    static float releaseFactorFor (float dbPerSecond) noexcept
    {
        if (dbPerSecond <= slowDecayDbPerSecond)
            return slowReleaseFactor;

        if (dbPerSecond >= fastDecayDbPerSecond)
            return fastReleaseFactor;

        const auto t = std::log (dbPerSecond / slowDecayDbPerSecond) / std::log (fastDecayDbPerSecond / slowDecayDbPerSecond);
        return slowReleaseFactor * std::pow (fastReleaseFactor / slowReleaseFactor, t);
    }

private:
    AdaptiveGateSettings settings;
    GateEnvelope envelope;
    double sampleRate = 48000.0;

    int stepSamples = 16, stepCounter = 0;
    int attackSlots = 30, decaySlots = 300, learnAfterSteps = 1500;
    std::array<float, 64> attackRing {};
    std::array<float, 640> decayRing {};
    int ringIndex = 0, decayIndex = 0;

    float floorDb = initialFloorDb, thresholdDb = initialFloorDb + 9.0f;
    float smoothedDb = initialFloorDb, smoothing = 0.99f, slewPerStep = 0.002f;
    int stretchSteps = 0;
    float stretchMin = 0.0f, stretchMax = 0.0f;
    double stretchSum = 0.0;

    int chatterSamples = 2400, sinceClose = 1 << 30;
    float peakHoldMs = 20.0f, holdBoost = 0.0f, holdRecoveryPerStep = 0.0003f;
    float releaseFactorNow = 1.0f;
    bool wasOpen = true;

    std::atomic<float> floorForUi { initialFloorDb };
    std::atomic<bool> frozenForUi { false };
};

} // namespace encore::dsp
