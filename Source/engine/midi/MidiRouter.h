// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <functional>

#include "engine/core/LockFreeFifo.h"
#include "engine/core/PatchPlayer.h"
#include "engine/core/RealtimeHandover.h"
#include "model/MidiMapping.h"
#include "model/Routing.h"

namespace encore
{

/** Turns foot controller and pedal MIDI into actions, on the audio thread.

    MIDI input (driver threads, possibly several devices at once)
        -> a queue (producers take turns on a spin lock among themselves; the
           audio thread never waits on it)
        -> the audio thread, at the start of each buffer: matches the bindings
           and acts at once: next/previous cue, program change recall, stomp
           toggles, expression pedals, mute. A busy screen can't delay any of it.

    Footswitches act when pressed (CC value going from below 64 to 64 or more,
    a note-on, a program change), never repeatedly while held.

    Expression pedals remember where they are, and a patch you switch to picks
    up the current position at once.

    MIDI out: when the cue changes (from MIDI, the keyboard or a click), a
    background thread sends that cue's messages to the chosen output.
*/
class MidiRouter final : public juce::MidiInputCallback
{
public:
    /** startSender = false for tests, which call sendPendingCueMessages themselves. */
    explicit MidiRouter (bool startSender = true);
    ~MidiRouter() override;

    //== Message thread ========================================================
    /** The mappings in use (the computer's, or the show's own). */
    void setBindings (const std::vector<MidiBinding>& bindings);

    /** "Mute output" bindings name an output; this finds its slot (-1: not
        here, the binding does nothing). Set once by the engine; call
        refreshOutputs() when the routing changes. */
    void setOutputResolver (std::function<int (const juce::String&)> slotForName);
    void refreshOutputs()   { setBindings (currentBindings); }

    /** The engine's per-output mutes, which "Mute output" toggles. */
    void setOutputMutes (std::array<std::atomic<bool>, (size_t) routing::maxOutputs>* mutes) noexcept { outputMutes = mutes; }
    void collectGarbage() { tables.collectGarbage(); }

    /** The messages each cue sends (index = cue position). */
    void setCueMidiOut (std::vector<std::vector<MidiOutMessage>> perCue);

    /** Where cue messages go (null = nowhere). */
    void setOutput (std::unique_ptr<juce::MidiOutput> output);

    /** While learning, messages are recorded but trigger nothing. */
    void setLearning (bool shouldLearn) noexcept { learning = shouldLearn; }

    /** The last message received (for MIDI learn and the activity display). */
    struct Received
    {
        int sequence = 0;    // increases with every message
        juce::uint8 status = 0, data1 = 0, data2 = 0;

        bool isPress() const noexcept;
        MidiTrigger toTrigger() const noexcept;
        juce::String describe() const;
    };

    Received getLastReceived() const noexcept;

    /** Presses of controls that milestone 7 handles (tuner, panic). */
    int getTunerPresses() const noexcept { return tunerPresses.load(); }
    int getPanicPresses() const noexcept { return panicPresses.load(); }

    /** Current pedal position (0..1), or -1 if it hasn't moved yet. */
    float getPedalPosition (int pedal) const noexcept;

    //== MIDI threads ==========================================================
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    /** Same, for tests and on-screen senders: any thread except the audio thread. */
    void receive (juce::uint8 status, juce::uint8 data1, juce::uint8 data2) noexcept;

    //== Audio thread ==========================================================
    /** Handles everything received since the last buffer. Call before the player renders. */
    void process (PatchPlayer& player, std::atomic<bool>& muted) noexcept;

    //== MIDI out (tests; the background thread uses this) =====================
    /** Sends the messages for cues entered since the last call to `send`. */
    void sendPendingCueMessages (const std::function<void (const juce::MidiMessage&)>& send);

private:
    struct Raw { juce::uint8 status = 0, data1 = 0, data2 = 0; };

    struct Table
    {
        std::vector<MidiBinding> bindings;
        std::vector<int> outputSlots;   // per binding: the output a "Mute output" toggles, or -1
    };

    void handle (const Raw& message, PatchPlayer& player, std::atomic<bool>& muted) noexcept;
    void act (const MidiBinding& binding, int outputSlot, const Raw& message, PatchPlayer& player, std::atomic<bool>& muted) noexcept;
    static bool matches (const MidiTrigger& trigger, const Raw& message) noexcept;

    // Input: many producers (serialised by the spin lock), one consumer (audio).
    juce::SpinLock producerLock;
    LockFreeFifo<Raw, 512> incoming;
    std::atomic<int> droppedMessages { 0 };

    RealtimeHandover<Table, 8> tables;
    Table* table = nullptr;                                  // audio thread
    std::vector<MidiBinding> currentBindings;                // message thread
    std::function<int (const juce::String&)> outputSlotFor;  // message thread
    std::array<std::atomic<bool>, (size_t) routing::maxOutputs>* outputMutes = nullptr;

    // Audio-thread state.
    std::array<std::array<juce::uint8, 128>, 16> lastCcValue {};
    Chain* lastActive = nullptr;
    int lastCue = -1;

    std::array<std::atomic<float>, (size_t) maxExpressionPedals + 1> pedals;
    std::atomic<int> lastReceivedPacked { 0 }, receivedCount { 0 };
    std::atomic<int> tunerPresses { 0 }, panicPresses { 0 };
    std::atomic<bool> learning { false };

    // MIDI out.
    LockFreeFifo<int, 64> cuesEntered;                       // audio -> sender thread
    juce::CriticalSection outLock;                           // never taken by the audio thread
    std::vector<std::vector<MidiOutMessage>> cueMidiOut;
    std::unique_ptr<juce::MidiOutput> output;

    class SenderThread;
    std::unique_ptr<SenderThread> sender;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRouter)
};

} // namespace encore
