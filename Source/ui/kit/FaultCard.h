// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A block switched off or not loaded (NEW-FEATURES-M10-M11 §A.3, §C): an
    amber card over the dimmed placeholder. A warning icon, the message (a
    bold lead such as "Switched off:" then the rest, wrapped), and up to two
    buttons (a primary action such as "Turn back on", then a standard one).

    The card draws its own drop shadow, so its bounds are the body expanded
    by getShadowMargin(): place it with boundsFor(). Text is laid out when
    it or the width changes, never in paint(). */
class FaultCard final : public juce::Component
{
public:
    FaultCard();

    void setMessage (const juce::String& lead, const juce::String& rest);

    /** Button texts; an empty text hides that button. */
    void setActions (const juce::String& primary, const juce::String& secondary);

    juce::TextButton& getPrimaryButton() noexcept   { return primary; }
    juce::TextButton& getSecondaryButton() noexcept { return secondary; }

    /** The body's height for its width (text lines, the buttons). */
    int getBodyHeight (int bodyWidth) const;

    /** The component bounds for a body at `body` (in the parent). */
    static juce::Rectangle<int> boundsFor (juce::Rectangle<int> body);
    static int getShadowMargin();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> getBody() const;
    juce::AttributedString makeText() const;
    void layOutText();

    juce::String lead, rest;
    juce::TextLayout text;
    juce::TextButton primary, secondary;
    juce::Image shadow;
    juce::Rectangle<int> shadowFor;
    float shadowScale = 0.0f;
};

} // namespace encore::kit
