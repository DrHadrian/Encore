// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "engine/core/Block.h"

namespace encore
{

/** An ordered list of blocks that processes a buffer in place.

    Built and prepared off the audio thread, then handed to the audio thread
    whole (see ChainHandover). Its block list never changes while it plays:
    to change the chain, build a new one and hand that over instead.

    Serial for now. Splits and parallel amps (later milestones) become a small
    graph inside this class; nothing outside it needs to change.
*/
class Chain
{
public:
    /** What keeps sounding when the patch is switched away from. */
    enum class TailMode
    {
        tailsOnly,   // only delay/reverb blocks (and what follows them) ring out
        fullChain    // the whole patch keeps processing its decaying signal
    };

    Chain() = default;

    /** Before handing over only. */
    Block& addBlock (std::unique_ptr<Block> block);

    const std::vector<std::unique_ptr<Block>>& getBlocks() const noexcept { return blocks; }
    Block* findBlock (const juce::String& typeId) const noexcept;

    /** Off the audio thread, while this chain isn't playing. */
    void prepare (const ProcessSpec& spec);
    const ProcessSpec& getSpec() const noexcept { return preparedSpec; }

    /** Message thread: lets blocks free what the audio thread released. */
    void collectGarbage();

    /** Audio thread. numSamples <= spec.maxBlockSize. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void reset() noexcept;

    int getLatencySamples() const noexcept;

    //== Patch switching =======================================================
    void setTailMode (TailMode mode) noexcept { tailMode.store (mode, std::memory_order_relaxed); }
    TailMode getTailMode() const noexcept     { return tailMode.load (std::memory_order_relaxed); }

    /** How a switch to and from this patch sounds (any thread).
        fadeIn: how quickly its input comes up when switched to.
        ringOut: how long it keeps getting the guitar (fading) when switched
        away from, so its amp and cab overlap the next patch instead of stopping. */
    void setSwitchFades (double fadeInSeconds, double ringOutSeconds) noexcept
    {
        fadeIn.store ((float) juce::jmax (0.001, fadeInSeconds), std::memory_order_relaxed);
        ringOut.store ((float) juce::jmax (0.001, ringOutSeconds), std::memory_order_relaxed);
    }

    float getFadeInSeconds() const noexcept  { return fadeIn.load (std::memory_order_relaxed); }
    float getRingOutSeconds() const noexcept { return ringOut.load (std::memory_order_relaxed); }

    static constexpr double defaultFadeInSeconds = 0.02;
    static constexpr double defaultRingOutSeconds = 0.15;

    /** Index of the first block with a tail, or -1 (computed by prepare()). */
    int getFirstTailBlock() const noexcept { return firstTailBlock; }

    /** Audio thread: process starting at a given block (earlier blocks are
        skipped: used to run only the tails of a patch that was switched away). */
    void processFrom (int firstBlock, float* const* channels, int numChannels, int numSamples) noexcept;

    /** Message thread: remember every parameter and on/off state as "saved". */
    void saveSnapshot();

    /** Audio thread: return to the saved state (ramped, no clicks). Atomics only. */
    void restoreSnapshot() noexcept;

    /** Free-form label for the UI and debugging (usually the patch name). */
    juce::String label;

private:
    std::vector<std::unique_ptr<Block>> blocks;
    ProcessSpec preparedSpec;
    juce::AudioBuffer<float> chainInput;   // copy of the dry input, for keyed blocks
    std::atomic<TailMode> tailMode { TailMode::tailsOnly };
    std::atomic<float> fadeIn { (float) defaultFadeInSeconds }, ringOut { (float) defaultRingOutSeconds };
    int firstTailBlock = -1;

    // Saved state, as atomics so the audio thread can restore it while the
    // message thread updates it. Sized when the chain is prepared.
    std::unique_ptr<std::atomic<float>[]> snapshotValues;
    std::unique_ptr<std::atomic<bool>[]> snapshotEnabled;
    size_t snapshotSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chain)
};

} // namespace encore
