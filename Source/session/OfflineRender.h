// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <vector>

#include "engine/core/Block.h"
#include "model/ShowModel.h"

namespace encore
{
class ShowFolder;
}

namespace encore::render
{

/** Offline rendering: a WAV through one block or a whole patch, with given
    settings, from the same engine code the app plays (native modules, stage
    B0, decision 0062). The core of the EncoreRender tool, and testable.

    Everything here runs on the calling thread (EncoreRender's main thread, a
    test): it loads, allocates and times freely. The blocks themselves run
    exactly as on the audio thread, buffer by buffer.
*/

/** How long one buffer took, per block (or for the whole chain). */
struct CpuStats
{
    juce::String name;          // block type (and id), or "patch"
    double averageMicros = 0.0;
    double worstMicros = 0.0;
    double averagePercent = 0.0;   // of the buffer's real-time duration
    double worstPercent = 0.0;
};

struct Result
{
    bool ok = false;
    juce::String error;                 // why it failed (ok == false)
    juce::StringArray warnings;         // e.g. a missing file in a patch (it still renders)

    juce::AudioBuffer<float> output;
    double sampleRate = 0.0;
    int blockSize = 0;
    int latencySamples = 0;

    float peakIn = 0.0f, peakOut = 0.0f, rmsIn = 0.0f, rmsOut = 0.0f;
    std::vector<CpuStats> cpu;
    juce::StringArray notes;            // what blocks say after the render (B2: a gate's floor, key, link, opens)

    /** A short text report: levels, latency, CPU per block, notes. */
    juce::String report() const;
};

/** Sets a block's parameters from a JSON object: { "id": value, ... }. A value
    is a number, or for a choice the name of a step ("Low cut"). Returns false,
    with the reason, for an unknown id or a value that isn't usable. */
bool applySettings (Block& block, const juce::var& settings, juce::String& error);

/** Renders `input` through a new block of `typeId` (hidden types included).
    A factory preset id, if given, is applied first; settings go on top.
    `refs` are the block's references (B2: keyInput, keyBlock, linkTo). Alone,
    a block has no earlier block or interface input, so those fall back and
    the notes say why; in a patch (renderPatch) they resolve. The guitar
    (ProcessContext::chainInput) is the input's first channel. */
/** One parameter moved linearly across the render, as an expression pedal
    would (B3): from `from` at the start to `to` at the end, set every buffer. */
struct Sweep
{
    juce::String parameterId;
    float from = 0.0f, to = 0.0f;
};

Result renderBlock (const juce::AudioBuffer<float>& input, double sampleRate, const juce::String& typeId,
                    const juce::var& settings, int blockSize = 64, const juce::String& presetId = {},
                    const std::map<juce::String, juce::String>& refs = {}, const std::optional<Sweep>& sweep = {});

/** Renders `input` (its first channel: the guitar) through a patch's whole chain. */
Result renderPatch (const juce::AudioBuffer<float>& input, double sampleRate, const PatchModel& patch,
                    const ShowFolder& folder, int blockSize = 64);

//== WAV files ==================================================================
struct Audio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
};

/** Reads a WAV (or AIFF). nullopt, with the reason, if it can't. */
std::optional<Audio> readAudioFile (const juce::File& file, juce::String& error);

/** Writes a 32-bit float WAV (bit-exact). */
bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, juce::String& error);

/** A log sweep from 20 Hz to 20 kHz at -6 dBFS, mono: test material. */
juce::AudioBuffer<float> makeSweep (double sampleRate, double seconds);

/** Generated guitar-like plucks, mono, deterministic: test material for
    listening (CI renders it; no recordings in the repo). An open low E, an
    A, then a palm-muted chug, a high D and a loud strummed-like accent,
    each a fundamental and falling harmonics with a 2 ms rise and an
    exponential decay, over a noise floor about 70 dB down. Scaled so the
    accent peaks at -1 dBFS; the other notes peak 5-9 dB lower. */
juce::AudioBuffer<float> makePlucks (double sampleRate);

/** Test material for the gates (B2), mono, deterministic, 10 s: 1.5 s of
    noise alone, a note ringing out, palm-muted chugs, a held chord decaying
    slowly, with stretches of noise between them, over a noise floor of the
    given peak (not normalised, so the floor stays there). The noise alone
    lets a Smart gate learn the floor from -70. */
juce::AudioBuffer<float> makeGateTest (double sampleRate, float noiseFloorDb = -60.0f);

/** A show for listening to the gates (B2), with these patches:
    - "Gate pair": the Gate pair around an amp (`ampModel`, a .nam file; an
      amp without one passes the signal through, with a warning);
    - "Ducking delay": a Pedal EQ (flat, the dry tap), a delay, and a Studio
      Gate with its duckDelay preset keyed from the dry tap;
    - "Smart X4": a Studio Gate in Smart mode with its highGain preset. */
ShowModel makeGateShow (const juce::File& ampModel);

} // namespace encore::render
