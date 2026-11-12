// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A gear knob (DESIGN-SYSTEM.md §8): a rotary slider in a 74-wide column,
    the 64 × 64 knob drawn by the LookAndFeel (ticks, knob, pointer), its
    name in capitals under it and an "EXP n" chip when a pedal moves it.
    Everything a juce::Slider does (drag, double-click reset, listeners)
    works as before. Real gear art later swaps the drawing only. */
class KnobView final : public juce::Slider
{
public:
    KnobView();

    void setLabel (const juce::String&);   // drawn in capitals
    void setChip (const juce::String&);    // "EXP 1", or empty

    /** Also show the value's text (getTextFromValue) under the label, as the
        plugin plate does ("35 %", the plugin's own text); the chip goes under it. */
    void setShowsValue (bool);

    /** 74 × 97: knob, gap, label, gap, chip (+ 13 with the value line). */
    static juce::Rectangle<int> idealBounds (bool withValue = false);

    /** Where the knob is drawn inside the component. */
    juce::Rectangle<int> getKnobBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void valueChanged() override;

private:
    juce::String labelCaps, chip, valueText;
    float chipWidth = 0.0f;
    bool showsValue = false;
};

/** The two-position gear switch (Level match): a 22 × 36 track, a light
    knob at the top when on, ON / OFF above and below, the name in between
    (one or two lines). A toggle button: clicking flips it, as before. */
class GearToggle final : public juce::Button
{
public:
    explicit GearToggle (const juce::String& name);

    /** 74 × 91 */
    static juce::Rectangle<int> idealBounds();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void resized() override;

private:
    juce::StringArray nameLines;   // in capitals, laid out to the column
};

/** An LED: red with a glow when on, dark red when off. `scale` 1 = 16 px. */
void paintLed (juce::Graphics&, juce::Rectangle<float> led, bool on, float scale = 1.0f);

/** The panel's power LED (28 px with a halo): lit while the block is on.
    A toggle button: clicking it turns the block on or off (the old ON/OFF
    button's job). */
class PowerLed final : public juce::Button
{
public:
    PowerLed();

    void setOn (bool);
    bool isOn() const noexcept { return getToggleState(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void resized() override;

private:
    juce::ColourGradient halo;
};

/** PREAMP / EQ / OUTPUT over a group of controls: centred caps text with a
    1 px rule on each side (none when `rules` is false). Paint helper: pass
    the text in capitals and its width from groupLabelWidth(), both made
    when the labels change. */
float groupLabelWidth (const juce::String& textCaps);
void paintGroupLabel (juce::Graphics&, juce::Rectangle<float> area, const juce::String& textCaps, float textWidth, bool rules);

} // namespace encore::kit
