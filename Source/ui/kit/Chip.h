// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A small label chip (NEW-FEATURES-M10-M11 §C): NOT HERE, OUTPUT MUTED,
    OFF, EXP n, VST3, counts. 16 px high, radius 3, 9.5 / 700 caps. */
enum class ChipStyle { mute, amber, exp, off, green, plain };

/** The chip's width for a text (padding included). */
float chipWidth (const juce::String& text);

/** Draws a chip in bounds (its height is the chip's; text centred). */
void paintChip (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& text, ChipStyle);

/** A chip as a component (sizes itself to the text with setText). */
class Chip final : public juce::Component
{
public:
    explicit Chip (ChipStyle = ChipStyle::amber);

    void setText (const juce::String&);
    void setStyle (ChipStyle);
    const juce::String& getText() const noexcept { return text; }

    /** The width it needs. */
    int getIdealWidth() const;

    void paint (juce::Graphics&) override;

private:
    juce::String text;
    ChipStyle style;
};

} // namespace encore::kit
