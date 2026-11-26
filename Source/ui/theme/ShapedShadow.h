// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace encore::theme
{

/** A drop shadow around a window with rounded corners, the shape the design
    draws (CSS box-shadow): the blurred rounded rectangle, outside the window
    only.

    JUCE's own window shadow (DropShadower) is drawn for a square window, so
    under a rounded menu it traced square corners: the "triangle" under each
    corner (Adrian, 20 Nov 2026). This replaces it for menus.

    The shadow is one click-through window right behind the owner (or a sibling
    component behind it when the owner sits inside another component). Its image
    is made once per size and screen scale, never per frame. */
class ShapedShadow final : private juce::ComponentListener
{
public:
    struct Look
    {
        float cornerRadius = 0.0f;
        juce::Colour colour;
        int blur = 0;               // the design's (CSS) blur: a Gaussian of half this, in logical px
        juce::Point<int> offset;
    };

    /** Gives `owner` its shadow until it is deleted (the owner keeps it).
        `onOwnerDeleted` runs as it goes. Does nothing for an owner that has one. */
    static void attach (juce::Component& owner, const Look&, std::function<void()> onOwnerDeleted = {});

    /** The owner's shadow window or component, or nullptr (tests). */
    static juce::Component* findLayer (const juce::Component& owner);

    /** Space the shadow needs around the owner: left, top, right, bottom. */
    static juce::BorderSize<int> marginsFor (const Look&);

    /** The shadow's pixels for an owner of `ownerSize` (logical px) at `scale`
        physical pixels per logical one: the layer's size plus the margins.
        Clear inside the owner's rounded shape. */
    static juce::Image render (juce::Point<int> ownerSize, const Look&, float scale);

    /** How often shadows were painted and the last clip (diagnostics for tests). */
    static juce::String paintsForTests();

    ~ShapedShadow() override;

private:
    struct Layer;

    ShapedShadow (juce::Component& owner, const Look&, std::function<void()> onOwnerDeleted);

    void update();
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void componentVisibilityChanged (juce::Component&) override;
    void componentBroughtToFront (juce::Component&) override;
    void componentParentHierarchyChanged (juce::Component&) override;
    void componentBeingDeleted (juce::Component&) override;

    juce::Component::SafePointer<juce::Component> owner;
    Look look;
    std::function<void()> onOwnerDeleted;
    std::unique_ptr<Layer> layer;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE (ShapedShadow)
};

} // namespace encore::theme
