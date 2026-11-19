// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "ui/kit/AmpFace.h"
#include "ui/kit/GearView.h"
#include "ui/kit/KnobView.h"

namespace encore::kit
{

/** An amp block's face in Edit (design/AMP-BUILDER.md §4): a GearView,
    600 × 430, showing the amp's composed face (AmpFace, static layer) with
    the controls that drive the block on top as real controls, in their
    style and size at their saved places:
    - a knob that drives Input, Bass, Mid, Treble or Output is a rotary
      slider (the owner binds it to the block's parameter: range, value,
      double-click default, the expression-pedal menu, as today's knobs);
    - a switch that drives Level match is a toggle button;
    - a Fixed knob or switch has a hover area only: "Fixed at 6 in this
      capture" / "Fixed up in this capture";
    - a light that follows the block lights while it's on, and turns it on
      or off when clicked (as the power LED does); an always-off light
      stays dark. A face without such a light gets the power LED at the
      top right of its panel.
    Every image is made when the face is built or a value changes, never in
    paint(). Message thread. */
class AmpFaceView final : public GearView
{
public:
    explicit AmpFaceView (const AmpPreset&);
    ~AmpFaceView() override;

    void setBlock (const BlockInfo&) override {}
    juce::Rectangle<int> getControlArea() const override;
    void setOn (bool) override;
    bool isOn() const noexcept { return on; }

    /** A knob or switch on the face drawn from a part, for the owner to bind. */
    class Knob;
    class Switch;
    class Light;

    /** The driving controls by the NamAmpBlock parameter they drive ("input", "normalise"...). */
    juce::Slider* findKnob (const juce::String& paramId) const;
    juce::Button* findSwitch (const juce::String& paramId) const;
    juce::StringArray getDrivenParameters() const;

    /** After setting knobs without a notification (binding them, a change from
        MIDI): redraws the knobs whose position changed. */
    void syncKnobs();

    /** A light that follows the block (or the power LED) was clicked. */
    std::function<void()> onPowerClicked;

    const AmpPreset& getAmp() const noexcept { return amp; }

    /** The tooltip at a point on the face (tests): a fixed control's, a light's, a knob's. */
    juce::String getTooltipAt (juce::Point<int>) const;

    /** Clicks a following light (or the LED), as a user would (tests). */
    void clickPower();

    void paint (juce::Graphics&) override;

private:
    class FixedArea;

    AmpPreset amp;
    juce::Image face;   // the static layer, composed once
    bool on = true;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Switch> switches;
    juce::OwnedArray<Light> lights;
    juce::OwnedArray<FixedArea> fixedAreas;
    std::unique_ptr<PowerLed> powerLed;   // only when no light follows the block
};

} // namespace encore::kit
