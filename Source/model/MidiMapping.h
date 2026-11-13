// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace encore
{

/** What a footswitch, pedal or other MIDI control does. Physical controls
    map to these named controls (per computer, or per show); each patch then
    decides what "Stomp 2" switches and which knobs "Expression 1" moves. */
enum class MidiAction
{
    nextCue,
    previousCue,
    programRecall,   // a program change goes to the cue with that number
    stomp,           // index: 1..8
    expression,      // index: 1..4
    mute,
    tuner,           // milestone 7
    panic,           // milestone 7
    muteOutput       // milestone 11: toggles one output's mute (MidiBinding::output)
};

/** Which incoming MIDI messages trigger a binding. */
struct MidiTrigger
{
    enum class Kind { controlChange, note, programChange };

    Kind kind = Kind::controlChange;
    int channel = 0;    // 1..16, or 0 = any channel
    int number = 0;     // CC or note number (ignored for program change)

    bool operator== (const MidiTrigger&) const = default;
};

struct MidiBinding
{
    MidiAction action = MidiAction::nextCue;
    int index = 1;      // stomp or expression number
    MidiTrigger trigger;
    juce::String output {};   // muteOutput: the output's name (this computer's routing)

    bool operator== (const MidiBinding&) const = default;
};

/** One expression assignment on a block parameter: heel = min, toe = max
    (min > max reverses the pedal). */
struct ExpressionAssignment
{
    int pedal = 1;
    float min = 0.0f, max = 1.0f;

    bool operator== (const ExpressionAssignment&) const = default;
};

/** A message sent to other gear (or the controller's LEDs) when a cue starts. */
struct MidiOutMessage
{
    enum class Kind { programChange, controlChange };

    Kind kind = Kind::programChange;
    int channel = 1;    // 1..16
    int number = 0;     // program, or CC number
    int value = 0;      // CC value

    bool operator== (const MidiOutMessage&) const = default;
};

constexpr int maxStomps = 8;
constexpr int maxExpressionPedals = 4;

/** Sensible defaults for a first start (and for testing with a MIDI keyboard):
    sustain pedal = next cue, CC 11 = expression 1, mod wheel = expression 2,
    program change = recall. */
std::vector<MidiBinding> makeDefaultMidiBindings();

//== Text, for the UI and the cue MIDI-out editor ==============================
juce::String describe (MidiAction action, int index);

/** What a binding does, in words: "Stomp 3", "Mute output: In-ear". */
juce::String describe (const MidiBinding& binding);
juce::String describe (const MidiTrigger& trigger);
juce::String describe (const MidiOutMessage& message);

/** "PC 1 5" (channel 1, program 5) or "CC 1 20 127" (channel 1, CC 20, value 127),
    one per line. Returns false and sets error for lines it can't read. */
bool parseMidiOut (const juce::String& text, std::vector<MidiOutMessage>& out, juce::String& error);
juce::String formatMidiOut (const std::vector<MidiOutMessage>& messages);

// JSON helpers (shared by show files and per-computer settings).
juce::var bindingsToVar (const std::vector<MidiBinding>& bindings);
std::vector<MidiBinding> bindingsFromVar (const juce::var& v);

} // namespace encore
