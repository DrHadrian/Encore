// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>

namespace encore
{

/** One knob on a block: gain, frequency, threshold...

    The value lives in an atomic, in real units (dB, Hz, ms), so the UI, MIDI
    and later expression pedals can set it from any thread at any time. The
    audio thread never jumps to a new value: it ramps there over a few
    milliseconds, which is what keeps fast knob sweeps free of zipper noise.

    A parameter can instead be a choice (a filter shape, a Quality setting):
    stepped values 0..n-1, each with a name, saved by index. A choice never
    ramps: it jumps to the new step, and the block that owns it crossfades its
    sound from the old setting to the new one (dsp::Crossfade, 10 ms).
*/
class Parameter
{
public:
    struct Spec
    {
        juce::String id;                   // stable, saved in show files: never rename
        juce::String name;                 // shown in the UI
        juce::NormalisableRange<float> range;
        float defaultValue = 0.0f;
        juce::String unit;                 // "dB", "Hz", "ms"...
        int decimals = 1;
        double rampSeconds = 0.03;
        std::function<juce::String (float)> textFunction {};   // optional: the value as text (e.g. a plugin's own); message thread

        /** Named steps. Non-empty makes this a choice: the range becomes
            0..n-1 in steps of 1, the value is the index, and it never ramps. */
        juce::StringArray choices {};

        /** Whether it can go on an expression pedal. Scenes may still mark it.
            False for settings that aren't a sound to sweep (B3: a Studio EQ
            band's "in use", its placement and mode, the phase mode...). */
        bool expressionAllowed = true;

        /** A choice parameter: values 0..names.size()-1, saved by index. */
        static Spec choice (juce::String id, juce::String name, juce::StringArray names, int defaultIndex = 0);
    };

    explicit Parameter (Spec specToUse);

    //== Any thread ============================================================
    void setValue (float newValue) noexcept;
    float getValue() const noexcept { return value.load (std::memory_order_relaxed); }
    void resetToDefault() noexcept  { setValue (spec.defaultValue); }

    const Spec& getSpec() const noexcept { return spec; }
    juce::String toText (float v) const;

    bool isChoice() const noexcept       { return ! spec.choices.isEmpty(); }
    int getNumChoices() const noexcept   { return spec.choices.size(); }

    /** The current step of a choice (0 for a parameter that isn't one). */
    int getChoiceIndex() const noexcept  { return isChoice() ? juce::roundToInt (getValue()) : 0; }

    //== Audio side ============================================================
    /** Set the ramp length. Call only while this parameter's block isn't processing. */
    void prepare (double sampleRate) noexcept;

    /** Call at the start of each block: picks up the latest target value. */
    void beginBlock() noexcept { smoother.setTargetValue (getValue()); }

    float nextValue() noexcept           { return smoother.getNextValue(); }
    float skip (int numSamples) noexcept { return smoother.skip (numSamples); }
    float currentValue() const noexcept  { return smoother.getCurrentValue(); }
    bool isSmoothing() const noexcept    { return smoother.isSmoothing(); }

    /** Jump to the target with no ramp (after a reset, when nothing is audible). */
    void snapToTarget() noexcept { smoother.setCurrentAndTargetValue (getValue()); }

private:
    static Spec sanitised (Spec);

    const Spec spec;
    std::atomic<float> value;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Parameter)
};

} // namespace encore
