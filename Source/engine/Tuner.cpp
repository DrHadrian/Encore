// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/Tuner.h"

namespace encore
{

juce::String Tuner::Reading::getNoteName() const
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((midiNote % 12) + 12) % 12];
}

void Tuner::prepare (double sampleRate)
{
    factor = juce::jmax (1, juce::roundToInt (sampleRate / 12000.0));
    decimatedRate = sampleRate / factor;
    phase = 0;

    // Two 2nd-order low-passes (12 dB/oct each) well below the new Nyquist,
    // still far above the highest note a guitar plays (~1.3 kHz).
    const auto corner = decimatedRate * 0.3;

    for (auto& filter : antiAlias)
    {
        filter.setCoefficients (dsp::BiquadCoefficients::lowPass (sampleRate, corner, 0.707));
        filter.reset();
    }

    scratch.assign ((size_t) detector.getRequiredSamples (decimatedRate), 0.0f);
    last = {};
}

void Tuner::setActive (bool shouldBeActive) noexcept
{
    active.store (shouldBeActive, std::memory_order_relaxed);
}

void Tuner::setReferencePitch (float hz) noexcept
{
    reference.store (juce::jlimit (415.0f, 466.0f, hz), std::memory_order_relaxed);
}

void Tuner::push (const float* input, int numSamples) noexcept
{
    if (! isActive() || input == nullptr)
        return;

    auto position = written.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        auto sample = input[i];

        for (auto& filter : antiAlias)
            sample = filter.processSample (0, sample);

        if (++phase >= factor)
        {
            phase = 0;
            ring[position % (juce::uint32) ringSize].store (sample, std::memory_order_relaxed);
            ++position;
        }
    }

    written.store (position, std::memory_order_release);
}

Tuner::Reading Tuner::analyse()
{
    const auto needed = (int) scratch.size();
    const auto end = written.load (std::memory_order_acquire);

    if (needed == 0 || needed > ringSize || end < (juce::uint32) needed)
        return last = {};

    // The newest samples. The writer only overwrites ones far older than
    // these (the ring holds more than twice what's read), so this is a clean copy.
    const auto start = end - (juce::uint32) needed;
    double energy = 0.0;

    for (int i = 0; i < needed; ++i)
    {
        scratch[(size_t) i] = ring[(start + (juce::uint32) i) % (juce::uint32) ringSize].load (std::memory_order_relaxed);
        energy += (double) scratch[(size_t) i] * scratch[(size_t) i];
    }

    Reading reading;
    reading.level = (float) std::sqrt (energy / needed);

    if (reading.level < silenceRms)
        return last = reading;

    const auto result = detector.detect (scratch.data(), needed, decimatedRate);

    if (result.frequency <= 0.0f)
        return last = reading;

    const auto semitones = 12.0f * std::log2 (result.frequency / getReferencePitch());
    const auto nearest = juce::roundToInt (semitones);

    reading.valid = true;
    reading.frequency = result.frequency;
    reading.midiNote = 69 + nearest;
    reading.cents = (semitones - (float) nearest) * 100.0f;

    // Steady needle: smooth while the note stays the same, jump when it changes.
    if (last.valid && last.midiNote == reading.midiNote)
        reading.cents = last.cents + 0.35f * (reading.cents - last.cents);

    return last = reading;
}

} // namespace encore
