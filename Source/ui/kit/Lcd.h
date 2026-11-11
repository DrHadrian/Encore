// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The toolbar display (DESIGN-SYSTEM.md §5.6): an inset blue-grey window
    with items, each a value over a small capital label. setItems() repaints
    only when something changed. */
class Lcd final : public juce::Component
{
public:
    struct Item
    {
        juce::String value, label;
        int minWidth = 0;   // keeps the layout still while values change

        bool operator== (const Item&) const = default;
    };

    explicit Lcd (bool stage = false);

    /** The component's height: the box plus the highlight line under it. */
    static int heightFor (bool stage) noexcept;

    void setItems (std::vector<Item>);

    /** The width the items need (with padding). */
    int getIdealWidth() const noexcept { return idealWidth; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Shown
    {
        Item item;
        juce::String caps;      // the label as drawn
        float x = 0, width = 0;
    };

    void layOut();
    juce::Rectangle<float> cellBounds (const Shown&) const;

    const bool isStage;
    std::vector<Shown> shown;
    int idealWidth = 0;
    juce::Path outline;
    juce::ColourGradient fill, shadow;
};

} // namespace encore::kit
