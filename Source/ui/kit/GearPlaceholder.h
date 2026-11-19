// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "ui/kit/GearView.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::kit
{

namespace type = theme::type;

/** Gear drawn as a placeholder (DESIGN-SYSTEM.md §8): a rounded box with
    the block's kind in capitals; pedals also show the name, an on/off LED
    and a footswitch; the amp panel has the control strip and the model
    plate. Pedals are drawn at 160 × 210 and scaled to the component (0.4 in
    the chain, 0.7 on Stage). Paints only; the owner handles clicks (it
    always did: ChainStrip, StageView).

    A pedal's drop shadow falls outside its box, so the owner paints it under
    the pedal with paintPedalShadow() (cached image, made once per size and
    screen scale). */
class GearPlaceholder final : public GearView
{
public:
    GearPlaceholder (Size, const BlockInfo& = {});

    void setBlock (const BlockInfo&) override;
    juce::Rectangle<int> getControlArea() const override;
    void setOn (bool) override;

    const BlockInfo& getBlock() const noexcept { return block; }

    /** The pedal's shadow (0, 6, 10 px, black 45 % at full size) under a
        pedal whose box is `pedalBounds` in the owner's coordinates. */
    static void paintPedalShadow (juce::Graphics&, juce::Rectangle<float> pedalBounds);

    /** The panel's kind label: its top this far above the bottom (default
        size::panelLabelBottom; the merge's box puts it lower). */
    void setLabelBottom (int fromBottom);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void update();   // text, shapes and fills, when the block or the size changes
    bool isPedal() const noexcept;

    BlockInfo block;
    juce::String kindCaps, plateCaps;
    float pedalScale = 1.0f;
    int labelBottom = theme::size::panelLabelBottom;

    juce::Path boxShape;
    juce::ColourGradient boxFill, footswitchFill;
    juce::Rectangle<float> labelArea, plateArea, stripArea, ledArea, nameArea, footswitchArea;
    type::Style kindStyle { 8.0f, type::Weight::bold };
    juce::Justification labelJustification = juce::Justification::centred;
};

} // namespace encore::kit
