// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/SmallKnob.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr float startAngle = -juce::MathConstants<float>::pi * 0.75f;
    constexpr float endAngle = juce::MathConstants<float>::pi * 0.75f;
}

SmallKnob::SmallKnob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRotaryParameters (startAngle + juce::MathConstants<float>::twoPi, endAngle + juce::MathConstants<float>::twoPi, true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Rectangle<int> SmallKnob::idealBounds()
{
    return { size::smallKnobColumn, size::smallKnob + size::smallKnobLabelGap + size::smallKnobLabelH + size::smallKnobValueGap + size::smallKnobValueH };
}

void SmallKnob::setLabel (const juce::String& text)
{
    labelCaps = paint::caps (type::smallKnobLabel, text);
    setTitle (text);
    repaint();
}

void SmallKnob::setBipolar (bool b)
{
    bipolar = b;
    repaint();
}

void SmallKnob::valueChanged()
{
    const auto text = getTextFromValue (getValue());

    if (text != valueText)
    {
        valueText = text;
        repaint();
    }
}

void SmallKnob::paint (juce::Graphics& g)
{
    const auto alpha = isEnabled() ? 1.0f : state::disabledAlpha;
    const auto side = (float) size::smallKnob;
    const auto knob = juce::Rectangle<float> (side, side).withCentre ({ (float) getWidth() * 0.5f, side * 0.5f });
    const auto c = knob.getCentre();
    const auto r = size::smallKnobArcR;
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle = startAngle + (endAngle - startAngle) * proportion;

    // Track, then the value arc (from the top for a bipolar control).
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    const juce::PathStrokeType stroke (size::smallKnobArcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (paint::c (colour::meterTrack).withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    const auto from = bipolar ? 0.0f : startAngle;

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (paint::c (colour::smallKnobArc).withMultipliedAlpha (alpha));
        g.strokePath (arc, stroke);
    }

    // The cap and its pointer.
    const auto capR = size::smallKnobCapR;
    g.setColour (paint::c (colour::smallKnobCap).withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
    g.setColour (paint::c (colour::knobBorder).withMultipliedAlpha (alpha));
    g.drawEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c), 1.0f);

    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
    g.setColour (paint::c (colour::knobPointer).withMultipliedAlpha (alpha));
    g.drawLine ({ c, c + dir * (capR - 1.0f) }, size::smallKnobPointerW);

    auto y = side + (float) size::smallKnobLabelGap;
    paint::text (g, labelCaps, { 0.0f, y, (float) getWidth(), (float) size::smallKnobLabelH }, type::smallKnobLabel,
                 colour::textSecondary, juce::Justification::centred);
    y += (float) (size::smallKnobLabelH + size::smallKnobValueGap);
    paint::text (g, valueText, { 0.0f, y, (float) getWidth(), (float) size::smallKnobValueH }, type::smallKnobValue,
                 colour::fieldText, juce::Justification::centred);
}

} // namespace encore::kit
