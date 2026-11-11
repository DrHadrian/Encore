// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Icons.h"

namespace encore::theme
{

/** How a button (or combo box) looks, chosen per component so screens never
    subclass controls just for their looks (DESIGN-SYSTEM.md §5.2). */
enum class Variant
{
    standard,   // raised grey
    primary,    // blue (Done, NEXT, fixes for problems)
    danger,     // raised grey, red bold text (PANIC in Edit, Mute on Stage)
    muted,      // red (MUTED)
    small,      // 18 px pane buttons (Full list, Load amp..., Reset counters)
    field       // combo box drawn as an inset field with a chevron
};

/** Where a button sits in a segmented control: only its outer corners round. */
enum class Segment { none, first, middle, last };

void setVariant (juce::Component&, Variant);
Variant getVariant (const juce::Component&);
void setSegment (juce::Component&, Segment);
Segment getSegment (const juce::Component&);

/** An icon in a text button: before the text ("Control panel") or after it
    (the chevron of "Show ▾"), 6 px from the text. */
void setButtonIcon (juce::Button&, Icon, float iconSize, bool afterText);

/** A key cap after the text ("Back to patches  F6"). */
void setButtonKeyCap (juce::Button&, const juce::String& key);

/** Encore's look for every stock JUCE control: buttons, checkboxes, combo
    boxes, menus, scrollbars, sliders, text fields, tooltips and alert boxes.
    One instance, created before the first window and destroyed after the
    last (see Main.cpp). */
class EncoreLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EncoreLookAndFeel();
    ~EncoreLookAndFeel() override;

    // Text
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getLabelFont (juce::Label&) override;

    // Buttons
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    int getTextButtonWidthToFitText (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool enabled, bool highlighted, bool down) override;

    // Combo boxes
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // Menus
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool highlighted,
                                       const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name,
                                                const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardHeight,
                                               int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int standardHeight,
                                                        int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override;
    void drawPopupMenuUpDownArrowWithOptions (juce::Graphics&, int width, int height, bool isScrollUpArrow,
                                              const juce::PopupMenu::Options&) override;

    // Scrollbars
    int getDefaultScrollbarWidth() override;
    bool areScrollbarButtonsVisible() override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool mouseDown) override;

    // Sliders
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float proportion,
                           float startAngle, float endAngle, juce::Slider&) override;

    // Text fields
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Tooltips
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    // Alert windows (small dialogs)
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    /** The knob placeholder (DESIGN-SYSTEM.md §8): tick scale, knob, pointer.
        Shared with the stage and gear views. `proportion` is 0..1. */
    static void drawKnob (juce::Graphics&, juce::Rectangle<float> cell, float proportion, bool enabled);

    /** Splits "Save now (Ctrl+S)" into "Save now" + "Ctrl+S" when the bracket
        holds a key; otherwise returns the text unchanged and no shortcut. */
    static std::pair<juce::String, juce::String> splitShortcut (const juce::String& text);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EncoreLookAndFeel)
};

} // namespace encore::theme
