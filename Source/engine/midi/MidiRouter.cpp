// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/midi/MidiRouter.h"

namespace encore
{

/** Sends cue MIDI out without ever touching the audio thread: it wakes every
    millisecond and sends whatever the audio thread queued. */
class MidiRouter::SenderThread final : public juce::Thread
{
public:
    explicit SenderThread (MidiRouter& r) : juce::Thread ("Encore MIDI out"), router (r) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            router.sendPendingCueMessages ([this] (const juce::MidiMessage& m)
            {
                if (router.output != nullptr)
                    router.output->sendMessageNow (m);
            });

            wait (1);
        }
    }

private:
    MidiRouter& router;
};

//==============================================================================
MidiRouter::MidiRouter (bool startSender)
{
    for (auto& p : pedals)
        p.store (-1.0f);

    setBindings (makeDefaultMidiBindings());

    if (startSender)
    {
        sender = std::make_unique<SenderThread> (*this);
        sender->startThread (juce::Thread::Priority::high);
    }
}

MidiRouter::~MidiRouter()
{
    if (sender != nullptr)
        sender->stopThread (1000);
}

void MidiRouter::setBindings (const std::vector<MidiBinding>& bindings)
{
    auto t = std::make_unique<Table>();
    t->bindings = bindings;
    tables.publish (std::move (t));
}

void MidiRouter::setCueMidiOut (std::vector<std::vector<MidiOutMessage>> perCue)
{
    const juce::ScopedLock sl (outLock);
    cueMidiOut = std::move (perCue);
}

void MidiRouter::setOutput (std::unique_ptr<juce::MidiOutput> newOutput)
{
    const juce::ScopedLock sl (outLock);
    output = std::move (newOutput);
}

float MidiRouter::getPedalPosition (int pedal) const noexcept
{
    return juce::isPositiveAndBelow (pedal, (int) pedals.size()) ? pedals[(size_t) pedal].load() : -1.0f;
}

//==============================================================================
bool MidiRouter::Received::isPress() const noexcept
{
    const auto type = status & 0xf0;
    return type == 0xc0 || (type == 0x90 && data2 > 0) || (type == 0xb0 && data2 >= 64);
}

MidiTrigger MidiRouter::Received::toTrigger() const noexcept
{
    MidiTrigger t;
    const auto type = status & 0xf0;
    t.channel = (status & 0x0f) + 1;
    t.number = data1;
    t.kind = type == 0xc0 ? MidiTrigger::Kind::programChange
           : type == 0x90 ? MidiTrigger::Kind::note
                          : MidiTrigger::Kind::controlChange;
    return t;
}

juce::String MidiRouter::Received::describe() const
{
    if (sequence == 0)
        return "nothing received yet";

    const auto channel = " (channel " + juce::String ((status & 0x0f) + 1) + ")";

    switch (status & 0xf0)
    {
        case 0xb0: return "CC " + juce::String (data1) + " = " + juce::String (data2) + channel;
        case 0x90: return "Note " + juce::MidiMessage::getMidiNoteName (data1, true, true, 3) + " on" + channel;
        case 0x80: return "Note " + juce::MidiMessage::getMidiNoteName (data1, true, true, 3) + " off" + channel;
        case 0xc0: return "Program change " + juce::String (data1) + channel;
        default:   return "Other message" + channel;
    }
}

MidiRouter::Received MidiRouter::getLastReceived() const noexcept
{
    const auto packed = lastReceivedPacked.load();
    Received r;
    r.sequence = receivedCount.load();
    r.status = (juce::uint8) ((packed >> 16) & 0xff);
    r.data1 = (juce::uint8) ((packed >> 8) & 0xff);
    r.data2 = (juce::uint8) (packed & 0xff);
    return r;
}

//==============================================================================
void MidiRouter::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.getRawDataSize() < 1 || message.getRawDataSize() > 3 || message.isSysEx())
        return;   // only channel messages: notes, CCs, program changes

    const auto* data = message.getRawData();
    receive (data[0],
             message.getRawDataSize() > 1 ? data[1] : (juce::uint8) 0,
             message.getRawDataSize() > 2 ? data[2] : (juce::uint8) 0);
}

void MidiRouter::receive (juce::uint8 status, juce::uint8 data1, juce::uint8 data2) noexcept
{
    const auto type = status & 0xf0;

    if (type != 0x80 && type != 0x90 && type != 0xb0 && type != 0xc0)
        return;   // ignore clock, aftertouch, pitch bend...

    // Several devices can deliver at once: they take turns here. The audio
    // thread only pops, so it never waits for this lock.
    {
        const juce::SpinLock::ScopedLockType sl (producerLock);

        if (! incoming.push ({ status, data1, data2 }))
            droppedMessages.fetch_add (1);
    }

    // Only presses and moves are worth showing (and learning).
    if (type != 0x80 && ! (type == 0x90 && data2 == 0))
    {
        lastReceivedPacked.store ((status << 16) | (data1 << 8) | data2);
        receivedCount.fetch_add (1);
    }
}

//==============================================================================
bool MidiRouter::matches (const MidiTrigger& trigger, const Raw& m) noexcept
{
    const auto type = m.status & 0xf0;
    const auto channel = (m.status & 0x0f) + 1;

    if (trigger.channel != 0 && trigger.channel != channel)
        return false;

    switch (trigger.kind)
    {
        case MidiTrigger::Kind::controlChange: return type == 0xb0 && m.data1 == trigger.number;
        case MidiTrigger::Kind::note:          return (type == 0x90 || type == 0x80) && m.data1 == trigger.number;
        case MidiTrigger::Kind::programChange: return type == 0xc0;
    }

    return false;
}

void MidiRouter::process (PatchPlayer& player, std::atomic<bool>& muted) noexcept
{
    while (auto* newer = tables.takeNext())
    {
        tables.retire (table);
        table = newer;
    }

    player.adoptLatestTable();   // act on the current running order

    Raw message;

    while (incoming.pop (message))
        handle (message, player, muted);

    // The patch changed (by MIDI, the keyboard or a click): give it the
    // pedals' current positions, and send the new cue's MIDI out.
    if (auto* active = player.getActiveChainNow(); active != lastActive)
    {
        lastActive = active;

        if (active != nullptr)
            for (int pedal = 1; pedal <= maxExpressionPedals; ++pedal)
                if (const auto position = pedals[(size_t) pedal].load(); position >= 0.0f)
                    active->applyExpression (pedal, position);
    }

    if (const auto cue = player.getCueIndexNow(); cue != lastCue)
    {
        lastCue = cue;

        if (cue >= 0)
            cuesEntered.push (cue);
    }
}

void MidiRouter::handle (const Raw& message, PatchPlayer& player, std::atomic<bool>& muted) noexcept
{
    const auto type = message.status & 0xf0;
    const auto channel = message.status & 0x0f;
    bool pressed = false;

    // A footswitch acts once per press, never repeatedly while held.
    if (type == 0xb0)
    {
        auto& previous = lastCcValue[(size_t) channel][(size_t) (message.data1 & 0x7f)];
        pressed = message.data2 >= 64 && previous < 64;
        previous = message.data2;
    }
    else
    {
        pressed = type == 0xc0 || (type == 0x90 && message.data2 > 0);
    }

    // While a control is being learned, pressing it must not also step the show.
    if (table == nullptr || learning.load (std::memory_order_relaxed))
        return;

    for (auto& binding : table->bindings)
    {
        if (! matches (binding.trigger, message))
            continue;

        if (binding.action == MidiAction::expression)
            act (binding, message, player, muted);   // continuous: every value counts
        else if (pressed)
            act (binding, message, player, muted);
    }
}

void MidiRouter::act (const MidiBinding& binding, const Raw& message, PatchPlayer& player, std::atomic<bool>& muted) noexcept
{
    switch (binding.action)
    {
        case MidiAction::nextCue:      player.nextNow(); break;
        case MidiAction::previousCue:  player.previousNow(); break;

        case MidiAction::programRecall:
            if ((message.status & 0xf0) == 0xc0)
                if (const auto cue = player.findCueForProgram (message.data1); cue >= 0)
                    player.goToCueNow (cue);
            break;

        case MidiAction::stomp:
            if (auto* chain = player.getActiveChainNow())
                chain->toggleStomp (binding.index);
            break;

        case MidiAction::expression:
        {
            if (! juce::isPositiveAndBelow (binding.index, (int) pedals.size()))
                break;

            // CC value (or note velocity) 0..127 -> 0..1.
            const auto position = (float) message.data2 / 127.0f;
            pedals[(size_t) binding.index].store (position);

            if (auto* chain = player.getActiveChainNow())
                chain->applyExpression (binding.index, position);
            break;
        }

        case MidiAction::mute:   muted.store (! muted.load()); break;
        case MidiAction::tuner:  tunerPresses.fetch_add (1); break;
        case MidiAction::panic:  panicPresses.fetch_add (1); break;
    }
}

//==============================================================================
void MidiRouter::sendPendingCueMessages (const std::function<void (const juce::MidiMessage&)>& send)
{
    int cue = 0;

    while (cuesEntered.pop (cue))
    {
        const juce::ScopedLock sl (outLock);

        if (! juce::isPositiveAndBelow (cue, (int) cueMidiOut.size()))
            continue;

        for (auto& m : cueMidiOut[(size_t) cue])
            send (m.kind == MidiOutMessage::Kind::programChange
                      ? juce::MidiMessage::programChange (m.channel, m.number)
                      : juce::MidiMessage::controllerEvent (m.channel, m.number, m.value));
    }
}

} // namespace encore
