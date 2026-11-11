// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/KnobView.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

//==============================================================================
KnobView::KnobView() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRotaryParameters (-juce::MathConstants<float>::pi * 0.75f + juce::MathConstants<float>::twoPi,
                         juce::MathConstants<float>::pi * 0.75f + juce::MathConstants<float>::twoPi, true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Rectangle<int> KnobView::idealBounds()
{
    return { size::knobColumn, size::knobCell + size::knobLabelGap + size::knobLabelH + size::knobLabelGap + size::chipH };
}

juce::Rectangle<int> KnobView::getKnobBounds() const
{
    return { (getWidth() - size::knobCell) / 2, 0, size::knobCell, size::knobCell };
}

void KnobView::setLabel (const juce::String& text)
{
    const auto caps = text.toUpperCase();

    if (caps != labelCaps)
    {
        labelCaps = caps;
        setTitle (text);
        repaint();
    }
}

void KnobView::setChip (const juce::String& text)
{
    if (text != chip)
    {
        chip = text;
        chipWidth = chip.isEmpty() ? 0.0f : textWidth (font (type::gearChip), chip) + (float) size::chipPadX * 2.0f;
        repaint();
    }
}

void KnobView::resized() {}

void KnobView::paint (juce::Graphics& g)
{
    const auto knob = getKnobBounds();
    getLookAndFeel().drawRotarySlider (g, knob.getX(), knob.getY(), knob.getWidth(), knob.getHeight(),
                                       (float) valueToProportionOfLength (getValue()),
                                       getRotaryParameters().startAngleRadians, getRotaryParameters().endAngleRadians, *this);

    auto y = (float) (knob.getBottom() + size::knobLabelGap);
    const auto alpha = isEnabled() ? 1.0f : state::disabledAlpha;
    g.setColour (paint::c (colour::knobLabelText).withMultipliedAlpha (alpha));
    g.setFont (font (type::knobLabel));
    g.drawText (labelCaps, juce::Rectangle<float> (0.0f, y, (float) getWidth(), (float) size::knobLabelH), juce::Justification::centred, true);

    if (chipWidth > 0.0f)
    {
        y += (float) (size::knobLabelH + size::knobLabelGap);
        const auto r = juce::Rectangle<float> (chipWidth, (float) size::chipH).withCentre ({ (float) getWidth() * 0.5f, y + (float) size::chipH * 0.5f });
        g.setColour (paint::c (colour::knobChipFill).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, (float) size::chipRadius);
        g.setColour (paint::c (colour::expChipText).withMultipliedAlpha (alpha));
        g.setFont (font (type::gearChip));
        g.drawText (chip, r, juce::Justification::centred, false);
    }
}

//==============================================================================
namespace
{
    // The toggle column, top to bottom: ON, track, name (two lines), OFF.
    constexpr auto onH = (float) size::toggleOnH, trackY = (float) size::toggleTrackY, nameY = (float) size::toggleNameY;
    constexpr auto nameLineH = (float) size::toggleNameLineH, offY = (float) size::toggleOffY;
}

GearToggle::GearToggle (const juce::String& name) : juce::Button (name)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle (name);
    nameLines.addTokens (name.toUpperCase(), " ", {});
    nameLines.removeEmptyStrings();

    // At most two lines: the first word, then the rest.
    while (nameLines.size() > 2)
    {
        nameLines.set (1, nameLines[1] + " " + nameLines[2]);
        nameLines.remove (2);
    }
}

juce::Rectangle<int> GearToggle::idealBounds()
{
    return { size::toggleColumn, size::toggleColumnH };
}

void GearToggle::resized() {}

void GearToggle::paintButton (juce::Graphics& g, bool, bool)
{
    const auto alpha = isEnabled() ? 1.0f : state::disabledAlpha;
    const auto w = (float) getWidth();
    const auto on = getToggleState();

    g.setFont (font (type::toggleLabel));
    g.setColour (paint::c (colour::gearGroupLabel).withMultipliedAlpha (alpha));
    g.drawText ("ON", juce::Rectangle<float> (0.0f, 0.0f, w, onH), juce::Justification::centred, false);
    g.drawText ("OFF", juce::Rectangle<float> (0.0f, offY, w, onH), juce::Justification::centred, false);

    const auto track = juce::Rectangle<float> ((w - (float) size::toggleW) * 0.5f, trackY, (float) size::toggleW, (float) size::toggleH);
    const auto radius = (float) size::toggleW * 0.5f;
    g.setColour (paint::c (colour::toggleTrack).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, radius);
    g.setColour (paint::c (colour::toggleBorder).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (track.reduced (0.5f), radius - 0.5f, 1.0f);

    const auto inset = ((float) size::toggleW - (float) size::toggleKnob) * 0.5f;
    const auto knob = juce::Rectangle<float> ((float) size::toggleKnob, (float) size::toggleKnob)
                          .withPosition (track.getX() + inset, on ? track.getY() + inset : track.getBottom() - inset - (float) size::toggleKnob);
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::thumbTop).withMultipliedAlpha (alpha), knob.getY(),
                                                       paint::c (colour::thumbBot).withMultipliedAlpha (alpha), knob.getBottom()));
    g.fillEllipse (knob);

    g.setFont (font (type::toggleName));
    g.setColour (paint::c (colour::knobLabelText).withMultipliedAlpha (alpha));
    const auto top = nameY + (nameLines.size() == 1 ? nameLineH * 0.5f : 0.0f);

    for (int i = 0; i < nameLines.size(); ++i)
        g.drawText (nameLines[i], juce::Rectangle<float> (0.0f, top + nameLineH * (float) i, w, nameLineH), juce::Justification::centred, true);
}

//==============================================================================
void paintLed (juce::Graphics& g, juce::Rectangle<float> led, bool on, float scale)
{
    if (on)
    {
        // box-shadow 0 0 8px red 80 %: two soft rings under the LED.
        const auto glow = (float) size::ledGlowR * scale;
        g.setColour (paint::c (colour::ledGlow).withMultipliedAlpha (0.25f));
        g.fillEllipse (led.expanded (glow * 0.75f));
        g.setColour (paint::c (colour::ledGlow).withMultipliedAlpha (0.45f));
        g.fillEllipse (led.expanded (glow * 0.35f));
    }

    g.setColour (paint::c (on ? colour::ledOn : colour::ledOff));
    g.fillEllipse (led);
}

PowerLed::PowerLed()
{
    setInterceptsMouseClicks (false, false);
}

void PowerLed::setOn (bool shouldBeOn)
{
    if (shouldBeOn != on)
    {
        on = shouldBeOn;
        setTitle (on ? "Power on" : "Power off");
        repaint();
    }
}

void PowerLed::resized()
{
    // radial-gradient (circle, halo core, mid 35 %, edge 70 %) over 54 px.
    const auto centre = getLocalBounds().toFloat().getCentre();
    const auto r = (float) size::ledHalo * 0.5f;
    halo = juce::ColourGradient (paint::c (colour::ledHaloCore), centre, paint::c (colour::ledHaloEdge), centre.translated (r * 0.7f, 0.0f), true);
    halo.addColour (0.5, paint::c (colour::ledHaloMid));
    setTitle (on ? "Power on" : "Power off");
}

void PowerLed::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto led = juce::Rectangle<float> ((float) size::powerLed, (float) size::powerLed).withCentre (bounds.getCentre());

    if (on)
    {
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> ((float) size::ledHalo, (float) size::ledHalo).withCentre (bounds.getCentre()));
    }

    paintLed (g, led, on);   // the 8 px glow, as on the pedals
}

//==============================================================================
float groupLabelWidth (const juce::String& textCaps)
{
    return textWidth (font (type::gearGroup), textCaps);
}

void paintGroupLabel (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& textCaps, float textW, bool rules)
{
    g.setFont (font (type::gearGroup));
    g.setColour (paint::c (colour::gearGroupLabel));
    g.drawText (textCaps, area, juce::Justification::centred, true);

    if (rules)
    {
        const auto gap = (float) size::gap;
        const auto y = area.getCentreY();
        g.setColour (paint::c (colour::groupRule));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), y, area.getCentreX() - textW * 0.5f - gap, y + 1.0f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getCentreX() + textW * 0.5f + gap, y, area.getRight(), y + 1.0f));
    }
}

} // namespace encore::kit
