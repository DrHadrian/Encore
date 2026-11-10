// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "engine/core/Chain.h"
#include "engine/core/LockFreeFifo.h"
#include "engine/core/RealtimeHandover.h"

namespace encore
{

/** Plays a show: owns every patch's chain, switches between them without a
    gap, and lets the old patch's tails ring out.

    Message thread:
      - adopt() takes a built chain (one per patch, all loaded at show open).
      - publish() sends the running order: a table of cues, each pointing at
        a chain. The audio thread looks cues up in it with no searching or
        allocation.
      - play(), next(), previous(), goToCue() queue commands. The audio thread
        acts on them at the start of its next buffer, so a busy screen never
        delays a patch change. MIDI (milestone 6) uses the same queue.
      - release() removes a chain. It is freed only once the audio thread
        provably can't reach it: it's in no table the audio thread still
        uses, and isn't playing or fading.

    Audio thread: process() renders the active patch plus up to three patches
    still ringing out, and handles switching:
      - the new patch's input fades in (20 ms by default) while the old one's
        fades out (150 ms by default, so its amp and cab overlap the new sound);
        the outputs are summed, so there is no gap and no click. Both times
        are set per patch (Chain::setSwitchFades);
      - the old patch then keeps running on silence until it falls below
        -90 dBFS (or 8 s pass). In "tails only" mode, after 200 ms of settling
        it runs only from its first delay/reverb block on, so the amp and cab
        stop costing CPU.
*/
class PatchPlayer
{
public:
    struct Cue
    {
        Chain* chain = nullptr;          // null: the cue keeps its place but changes nothing
        bool resetIfSamePatch = false;   // same patch as the previous cue: reset to saved settings
    };

    static constexpr int maxFading = 3;
    static constexpr double tailSettleSeconds = 0.2;
    static constexpr double tailTimeoutSeconds = 8.0;
    static constexpr float silenceThreshold = 3.0e-5f;   // about -90 dBFS

    PatchPlayer();
    ~PatchPlayer();

    //== Message thread ========================================================
    /** Takes ownership; prepares it if the device is running. */
    Chain* adopt (std::unique_ptr<Chain> chain);

    /** The chain will be deleted once the audio thread can no longer reach it.
        Publish a table without it first (or together with releasing). */
    void release (Chain* chain);

    /** Replaces the running order. Every adopted, unreleased chain stays
        playable with play(); cues refer to them. Returns the table's number. */
    int publish (std::vector<Cue> cues);

    void play (Chain* chain);   // a patch directly (edit view), cue position unchanged
    void next();
    void previous();
    void goToCue (int index);

    /** Moves the cue position without changing the sound (after the running
        order was edited, so "next" continues from the right place). */
    void setCuePosition (int index);

    /** Frees released chains and old tables that are no longer reachable. Call regularly. */
    void collectGarbage();

    /** Re-prepares every chain. Only while audio is stopped (device about to start). */
    void prepareAll (const ProcessSpec& spec);

    /** What the audio thread is playing (for the UI). */
    Chain* getActiveChain() const noexcept { return activeForUi.load(); }
    int getCurrentCue() const noexcept     { return cueForUi.load(); }
    int getNumRinging() const noexcept     { return ringingForUi.load(); }
    int getNumOwnedChains() const;

    /** Commands the audio thread had to drop because the queue was full (should stay 0). */
    int getDroppedCommands() const noexcept { return droppedCommands.load(); }

    //== Audio thread ==========================================================
    /** Mono, in place: the guitar in, the patches' mix out. numSamples <= prepared block size. */
    void process (float* io, int numSamples) noexcept;

private:
    struct Table
    {
        int generation = 0;
        std::vector<Chain*> chains;   // everything play() may use
        std::vector<Cue> cues;
    };

    struct Command
    {
        enum class Type { play, next, previous, goTo, setPosition } type = Type::next;
        Chain* chain = nullptr;
        int index = 0;
    };

    struct Slot
    {
        Chain* chain = nullptr;
        float inputGain = 0.0f;         // current input level into the chain
        float inputTarget = 0.0f;       // 1 = active, 0 = ringing out
        float inputStep = 1.0f;         // per-sample change towards the target
        float outputGain = 1.0f;        // ramps to 0 when a ringing patch is cut short
        bool cutting = false;
        int activeFor = 0;              // samples it played as the active patch
        int age = 0;                    // samples since it started ringing out
        int silentFor = 0;              // consecutive quiet samples
    };

    void sendCommand (const Command&);
    void applyCommand (const Command&) noexcept;
    void switchTo (Chain* target, bool resetIfSame) noexcept;
    bool tableContains (const Chain*) const noexcept;
    float stepFor (float seconds) const noexcept;
    void renderSlot (Slot&, const float* input, float* out, int numSamples, bool ringing) noexcept;
    void publishSlotsForUi() noexcept;

    // Message-thread side.
    mutable juce::CriticalSection ownedLock;   // guards against prepareAll() from a driver thread
    std::vector<std::unique_ptr<Chain>> owned;
    struct Released { Chain* chain; int generation; };
    std::vector<Released> released;
    int latestGeneration = 0;
    ProcessSpec spec;

    RealtimeHandover<Table, 8> tables;
    LockFreeFifo<Command, 128> commands;

    // Audio-thread side.
    Table* table = nullptr;
    Slot active;
    std::array<Slot, (size_t) maxFading> ringing;
    int cueIndex = -1;
    std::vector<float> inputCopy, slotBuffer, mix;
    double sampleRate = 48000.0;
    int settleSamples = 0, timeoutSamples = 0, silenceHoldSamples = 0;

    // Shared: what the audio thread uses, so freeing and the UI stay correct.
    std::atomic<int> audioGeneration { 0 };
    std::array<std::atomic<Chain*>, (size_t) maxFading + 1> inUse {};
    std::atomic<Chain*> activeForUi { nullptr };
    std::atomic<int> cueForUi { -1 }, ringingForUi { 0 }, droppedCommands { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchPlayer)
};

} // namespace encore
