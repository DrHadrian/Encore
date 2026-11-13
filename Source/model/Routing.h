// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace encore
{

/** Routing (milestone 11): this computer's named inputs and outputs.

    Hardware names live on the computer, shows refer to them by name (like
    MIDI mappings and plugins): "Guitar" is input 1 on one rig and input 3 on
    another, "In-ear" is outputs 3/4 here and 1/2 there, and the same show plays
    right on both. Saved in the computer's settings, never in the show.

    Channels are 1-based, as on the interface's labels. An output with no
    channels is "all outputs": the default "Main", which plays exactly as
    Encore did before routing (output 1 = left, every other output = right). */
namespace routing
{
    constexpr int maxInputs = 8;
    constexpr int maxOutputs = 8;
    constexpr int maxChannel = 64;        // 1-based; more than any interface Encore meets
    constexpr int maxNameLength = 32;

    constexpr float silentDb = -100.0f;   // at or below: -inf (off)
    constexpr float maxLevelDb = 6.0f;    // output levels, sends, merge levels
    constexpr float maxDelayMs = 100.0f;  // per-output delay, to line up with a far PA
    constexpr float delayStepMs = 0.1f;
    constexpr float speedOfSound = 343.0f; // m/s, for "12.0 ms ≈ 4.12 m"

    constexpr auto defaultInputName = "Guitar";
    constexpr auto defaultOutputName = "Main";

    /** dB to gain, with silentDb and below = 0. */
    float gainFor (float dB) noexcept;

    /** A level kept in -inf..+6 dB (anything at or below silentDb becomes silentDb). */
    float clampLevel (float dB) noexcept;

    /** A trimmed name, at most maxNameLength characters. */
    juce::String cleanName (const juce::String& name);

    /** Names are matched ignoring case ("in-ear" is "In-ear"). */
    bool sameName (const juce::String& a, const juce::String& b) noexcept;
}

/** The output EQ: the EQ block's controls, without the mid width (fixed). */
struct OutputEq
{
    float lowCut = 20.0f;       // 20..400 Hz, 20 = off
    float highCut = 20000.0f;   // 2..20 kHz, 20 k = off
    float bass = 0.0f;          // +-12 dB shelf
    float mid = 0.0f;           // +-12 dB peak...
    float midFreq = 800.0f;     // ...at 200..5000 Hz
    float treble = 0.0f;        // +-12 dB shelf

    static constexpr float minLowCut = 20.0f, maxLowCut = 400.0f;
    static constexpr float minHighCut = 2000.0f, maxHighCut = 20000.0f;
    static constexpr float maxGainDb = 12.0f;
    static constexpr float minMidFreq = 200.0f, maxMidFreq = 5000.0f;
    static constexpr float midQ = 0.9f;

    /** True when it changes nothing (all gains 0, both cuts off). */
    bool isFlat() const noexcept;

    /** Every value inside its range. */
    OutputEq clamped() const noexcept;

    bool operator== (const OutputEq&) const = default;
};

struct RoutingInput
{
    juce::String name;
    int channel = 1;            // 1-based

    bool operator== (const RoutingInput&) const = default;
};

struct RoutingOutput
{
    juce::String name;
    std::vector<int> channels;  // 1-based: 1 = mono, 2 = a stereo pair; empty = all outputs
    float levelDb = 0.0f;       // -inf (silentDb)..+6
    bool mute = false;          // saved: a muted output stays muted after a restart
    OutputEq eq;
    float delayMs = 0.0f;       // 0..100

    bool isAllOutputs() const noexcept   { return channels.empty(); }
    bool isStereo() const noexcept       { return channels.size() == 2 || channels.empty(); }

    bool operator== (const RoutingOutput&) const = default;
};

struct RoutingConfig
{
    std::vector<RoutingInput> inputs;     // at least one
    std::vector<RoutingOutput> outputs;   // at least one

    /** By name, ignoring case; null when this computer doesn't have it. */
    const RoutingInput* findInput (const juce::String& name) const;
    const RoutingOutput* findOutput (const juce::String& name) const;
    RoutingOutput* findOutput (const juce::String& name);

    int indexOfInput (const juce::String& name) const;    // -1 when not here
    int indexOfOutput (const juce::String& name) const;

    /** The input a patch asking for this name plays: that input, or the first
        when the name is empty or not on this computer. */
    const RoutingInput& inputFor (const juce::String& name) const;

    /** A name not used yet by an input (or output): base, else "base 2"... */
    juce::String uniqueInputName (const juce::String& base) const;
    juce::String uniqueOutputName (const juce::String& base) const;

    /** Fixes what a hand-edited or damaged file could get wrong: empty or
        duplicate names, too many entries, channels and values out of range,
        no inputs or outputs. Never fails: the result is always usable. */
    RoutingConfig sanitised() const;

    bool operator== (const RoutingConfig&) const = default;
};

/** The first-start routing: input "Guitar" on the given (1-based) channel and
    output "Main" on all outputs, which plays exactly as before routing. */
RoutingConfig makeDefaultRouting (int guitarChannel = 1);

/** "1/2 · stereo", "5 · mono", "all outputs". */
juce::String describeChannels (const RoutingOutput& output);

/** "12.0 ms", "≈ 4.12 m". */
/** "1/2", "5", "all" (the bottom row's strips). */
juce::String describeShortChannels (const RoutingOutput& output);

/** "0.0 dB", "−6.0 dB" (a real minus), "Off" when silent or switched off. */
juce::String describeLevel (float dB, bool on = true);

juce::String describeDelay (float ms);
juce::String describeDistance (float ms);

// JSON (per-computer settings).
juce::var routingToVar (const RoutingConfig& config);
RoutingConfig routingFromVar (const juce::var& v, int guitarChannel = 1);

} // namespace encore
