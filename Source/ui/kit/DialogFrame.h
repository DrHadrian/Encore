// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/kit/IconButton.h"
#include "ui/theme/EncoreLookAndFeel.h"

namespace encore::kit
{

/** A dialog body (DESIGN-SYSTEM.md §5.9): radius 10, a 30 px title bar with
    the title centred and a close ✕, optional icon tabs (Settings), the
    content, and a 48 px footer with help text left and buttons right. The
    content component belongs to the owner. Escape and ✕ call onClose. The
    drop shadow and the dimmed window behind are DialogOverlay's. */
class DialogFrame final : public juce::Component
{
public:
    /** `closeWhatItDoes` is the ✕ button's title and tooltip ("Close settings"). */
    DialogFrame (const juce::String& title, const juce::String& closeWhatItDoes);
    ~DialogFrame() override;

    void setDialogTitle (const juce::String&);
    const juce::String& getDialogTitle() const noexcept { return title; }

    struct Tab
    {
        juce::String name;
        theme::Icon icon;
    };

    /** Icon tabs under the title (none: no strip). */
    void setTabs (const std::vector<Tab>&);
    void setSelectedTab (int index);
    int getSelectedTab() const noexcept { return selectedTab; }
    std::function<void (int)> onTabChange;

    /** The body between the title (or tabs) and the footer. Not owned. */
    void setContent (juce::Component*);
    juce::Rectangle<int> getContentBounds() const;

    /** Footer: help text left; buttons right, in the order added
        (Cancel, then Done). `showFooter (false)` for dialogs without one. */
    void setFooterText (const juce::String&);
    juce::TextButton& addFooterButton (const juce::String& text, theme::Variant = theme::Variant::standard);
    void showFooter (bool);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class TabButton;

    int titleBottom() const noexcept;
    int tabsBottom() const noexcept;
    int footerTop() const noexcept;

    juce::String title, footerText;
    IconButton closeButton;
    juce::OwnedArray<TabButton> tabs;
    juce::OwnedArray<juce::TextButton> footerButtons;
    juce::Component* content = nullptr;
    int selectedTab = -1;
    bool hasFooter = true;

    juce::Path bodyShape, titleShape;
    juce::ColourGradient titleFill, tabFill;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DialogFrame)
};

/** Over the whole window: the dimmer (black 45 %, a flat fill) and the
    dialog's drop shadow (rendered once per size and screen scale), with the
    frame centred on top. The owner adds it to the top-level component,
    sizes it to fill it and calls enterModalState(); clicks outside the frame
    do nothing (as with today's modal windows). */
class DialogOverlay final : public juce::Component
{
public:
    DialogOverlay (DialogFrame&, juce::Rectangle<int> frameSize);

    /** Where the frame goes: its size centred, shrunk to fit small windows. */
    juce::Rectangle<int> getFrameBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DialogFrame& frame;
    const juce::Rectangle<int> wantedSize;
    juce::Image shadow;
    float shadowScale = 0.0f;
    juce::Rectangle<int> shadowFor;
};

} // namespace encore::kit
