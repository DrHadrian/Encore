// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A small UI knob (NEW-FEATURES-M10-M11 §C, the output EQ): 36 px, a
    270° track with the value arc (from the start, or from the top for a
    bipolar control such as a ±12 dB gain), a dark cap and a pointer; its
    name in capitals and its value (textFromValueFunction) under it. A
    juce::Slider: dragging, double-click reset and listeners work as usual. */
class SmallKnob final : public juce::Slider
{
public:
    SmallKnob();

    void setLabel (const juce::String&);
    void setBipolar (bool);

    /** 70 × 66: knob, label, value. */
    static juce::Rectangle<int> idealBounds();

    void paint (juce::Graphics&) override;
    void valueChanged() override;

private:
    juce::String labelCaps, valueText;
    bool bipolar = false;
};

} // namespace encore::kit
