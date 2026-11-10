// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cmath>

#include "engine/dsp/Biquad.h"
#include "engine/dsp/PitchDetector.h"

namespace encore
{

/** The tuner. While it's open, the audio thread low-passes and decimates the
    raw guitar input (to about 12 kHz, plenty for any guitar fundamental) into
    a ring of atomics: a handful of operations per sample, no locks. The screen
    reads the newest stretch of that ring ~20 times a second and runs the
    pitch detector there, off the audio thread.
*/
class Tuner
{
public:
    struct Reading
    {
        bool valid = false;
        float frequency = 0.0f;   // Hz
        int midiNote = 0;         // nearest note, 69 = A4
        float cents = 0.0f;       // -50..+50 from that note, smoothed
        float level = 0.0f;       // RMS of the analysed stretch

        /** "E", "F#", ... (sharps, like most tuners). */
        juce::String getNoteName() const;
        int getOctave() const noexcept { return midiNote / 12 - 1; }
        bool isInTune (float toleranceCents = 2.0f) const noexcept { return valid && std::abs (cents) <= toleranceCents; }
    };

    /** Message or device thread, while audio isn't running. */
    void prepare (double sampleRate);

    /** Open/close. Closed costs nothing on the audio thread. Any thread. */
    void setActive (bool shouldBeActive) noexcept;
    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    /** Concert pitch of A4 (415..466 Hz; 440 by default). Any thread. */
    void setReferencePitch (float hz) noexcept;
    float getReferencePitch() const noexcept { return reference.load (std::memory_order_relaxed); }

    /** AUDIO THREAD: the raw guitar input. Real-time safe. */
    void push (const float* input, int numSamples) noexcept;

    /** MESSAGE THREAD: detect the pitch in the newest input. */
    Reading analyse();

    static constexpr float silenceRms = 0.001f;   // about -60 dBFS: nothing played

private:
    static constexpr int ringSize = 4096;   // ~0.34 s at 12 kHz: room for the reader

    std::atomic<bool> active { false };
    std::atomic<float> reference { 440.0f };

    // Audio-thread state.
    std::array<dsp::Biquad<1>, 2> antiAlias;
    int factor = 4, phase = 0;
    std::array<std::atomic<float>, (size_t) ringSize> ring {};
    std::atomic<juce::uint32> written { 0 };

    // Message-thread state.
    double decimatedRate = 12000.0;
    dsp::PitchDetector detector;
    std::vector<float> scratch;
    Reading last;
};

} // namespace encore
