// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The toolbars' status bar (design v3.2, V3.2-CHANGES §5; replaces the blue
    LCD there, which stays for the tuner readout): fields side by side, each a
    small capital label above its value, divided by 1 px lines, on a flat dark
    box. setItems() repaints only what changed. */
class StatusBar final : public juce::Component
{
public:
    struct Item
    {
        juce::String value, label;
        int minWidth = 0;   // the value's room: keeps the layout still while values change
        int maxWidth = 0;   // 0: as wide as the text; else longer text is cut with "..."

        bool operator== (const Item&) const = default;
    };

    explicit StatusBar (bool stage = false);

    /** The component's height: 32 (Edit) or 36 (Stage) inside a 1 px border. */
    static int heightFor (bool stage) noexcept;

    void setItems (std::vector<Item>);

    /** The room there is: items with a maxWidth are cut further (down to
        lcdCutMinW) so the bar fits. 0: no limit. */
    void setAvailableWidth (int);

    /** The width the fields need (with padding, lines and border). */
    int getIdealWidth() const noexcept { return idealWidth; }

    std::vector<Item> getItems() const;

    void paint (juce::Graphics&) override;

private:
    struct Shown
    {
        Item item;
        juce::String caps;      // the label as drawn
        float x = 0, width = 0; // the field's text column
    };

    void layOut();
    juce::Rectangle<float> cellBounds (const Shown&) const;

    const bool isStage;
    std::vector<Shown> shown;
    int idealWidth = 0, availableWidth = 0;
};

} // namespace encore::kit
