// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/EncoreLookAndFeel.h"

#include <optional>
#include <tuple>

#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"
#include "ui/kit/KeyCap.h"

namespace encore::theme
{

namespace
{
    const juce::Identifier variantId ("encoreVariant"), segmentId ("encoreSegment");

    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    /** Menu geometry (DESIGN-SYSTEM.md §5.8, measured on the boards). */
    constexpr int menuTextIndent = 24;   // from the item's left edge
    constexpr int menuTickInset = 6;
    constexpr int menuRightPad = 10;
    constexpr int menuArrowWidth = 6;
    constexpr int menuShortcutGap = 24;
    constexpr int menuHeaderHeight = 22;
    constexpr int tooltipPadX = 8, tooltipPadY = 6;
    constexpr int alertTitleBar = size::dialogTitleBar;

    bool isAlertButton (const juce::Button& b)
    {
        return dynamic_cast<const juce::AlertWindow*> (b.getParentComponent()) != nullptr;
    }

    /** In an alert window, the first button confirms (OK, Delete, Rename...). */
    bool isFirstAlertButton (const juce::Button& b)
    {
        if (auto* parent = b.getParentComponent())
            for (auto* child : parent->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    return button == &b;

        return false;
    }

    bool isDestructive (const juce::String& text)
    {
        return text == "Delete" || text == "Remove";
    }

    Variant effectiveVariant (const juce::Button& b)
    {
        const auto v = getVariant (b);

        if (v == Variant::standard && isAlertButton (b) && isFirstAlertButton (b))
            return isDestructive (b.getButtonText()) ? Variant::danger : Variant::primary;

        return v;
    }

    /** Rounded rect with only some corners rounded (segmented controls). */
    juce::Path segmentShape (juce::Rectangle<float> r, float radius, Segment segment)
    {
        const auto left = segment == Segment::none || segment == Segment::first;
        const auto right = segment == Segment::none || segment == Segment::last;
        juce::Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius, radius, left, right, left, right);
        return p;
    }

    bool looksLikeKey (const juce::String& s)
    {
        // "Ctrl+S", "Ctrl+Up", "F9", "G", "Enter", "Esc", "I" ...
        if (s.startsWith ("Ctrl+") || s.startsWith ("Shift+") || s.startsWith ("Alt+"))
            return true;

        if (s.length() >= 2 && s.length() <= 3 && s[0] == 'F' && s.substring (1).containsOnly ("0123456789"))
            return true;

        return s == "Enter" || s == "Esc" || s == "Space" || (s.length() == 1 && juce::CharacterFunctions::isUpperCase (s[0]));
    }
}

//==============================================================================
void setVariant (juce::Component& c, Variant v)        { c.getProperties().set (variantId, (int) v); c.repaint(); }
Variant getVariant (const juce::Component& c)          { return (Variant) (int) c.getProperties().getWithDefault (variantId, 0); }
void setSegment (juce::Component& c, Segment s)        { c.getProperties().set (segmentId, (int) s); c.repaint(); }
Segment getSegment (const juce::Component& c)          { return (Segment) (int) c.getProperties().getWithDefault (segmentId, 0); }

namespace
{
    const juce::Identifier iconId { "encoreIcon" }, iconSizeId { "encoreIconSize" }, iconAfterId { "encoreIconAfter" };
    constexpr int iconGap = 6;

    /** The button's icon, if it has one: icon, size, after the text. */
    std::optional<std::tuple<Icon, float, bool>> buttonIcon (const juce::Component& c)
    {
        const auto& p = c.getProperties();

        if (! p.contains (iconId))
            return std::nullopt;

        return std::tuple<Icon, float, bool> { (Icon) (int) p[iconId], (float) p[iconSizeId], (bool) p[iconAfterId] };
    }
}

void setButtonKeyCap (juce::Button& b, const juce::String& key)
{
    b.getProperties().set ("encoreKeyCap", key);
    b.repaint();
}

void setButtonIcon (juce::Button& b, Icon icon, float iconSize, bool afterText)
{
    auto& p = b.getProperties();
    p.set (iconId, (int) icon);
    p.set (iconSizeId, iconSize);
    p.set (iconAfterId, afterText);
    b.repaint();
}

std::pair<juce::String, juce::String> EncoreLookAndFeel::splitShortcut (const juce::String& text)
{
    if (text.endsWithChar (')'))
    {
        const auto open = text.lastIndexOf (" (");

        if (open > 0)
        {
            const auto inside = text.substring (open + 2, text.length() - 1);

            if (looksLikeKey (inside))
                return { text.substring (0, open), inside };
        }
    }

    return { text, {} };
}

//==============================================================================
EncoreLookAndFeel::EncoreLookAndFeel()
{
    // Every stock component that draws text in the default font gets Inter.
    setDefaultSansSerifTypeface (EncoreFonts::get().typeface (type::Weight::regular));

    using namespace colour;
    const std::pair<int, juce::uint32> colourIds[] = {
        { juce::ResizableWindow::backgroundColourId,  window },
        { juce::DocumentWindow::textColourId,         text },
        { juce::Label::textColourId,                  text },
        { juce::Label::backgroundColourId,            0 },
        { juce::Label::outlineColourId,               0 },
        { juce::TextButton::buttonColourId,           buttonTop },
        { juce::TextButton::buttonOnColourId,         pressedTop },
        { juce::TextButton::textColourOffId,          colour::text },
        { juce::TextButton::textColourOnId,           textStrong },
        { juce::ToggleButton::textColourId,           colour::text },
        { juce::ToggleButton::tickColourId,           textStrong },
        { juce::ToggleButton::tickDisabledColourId,   textDisabled },
        { juce::ComboBox::backgroundColourId,         fieldFill },
        { juce::ComboBox::textColourId,               fieldText },
        { juce::ComboBox::outlineColourId,            fieldBorder },
        { juce::ComboBox::arrowColourId,              textSecondary },
        { juce::ComboBox::focusedOutlineColourId,     accent },
        { juce::PopupMenu::backgroundColourId,        menu },
        { juce::PopupMenu::textColourId,              colour::text },
        { juce::PopupMenu::headerTextColourId,        textDisabled },
        { juce::PopupMenu::highlightedBackgroundColourId, menuHighlight },
        { juce::PopupMenu::highlightedTextColourId,   textStrong },
        { juce::ListBox::backgroundColourId,          pane },
        { juce::ListBox::outlineColourId,             0 },
        { juce::ListBox::textColourId,                textRow },
        { juce::TextEditor::backgroundColourId,       fieldFill },
        { juce::TextEditor::textColourId,             fieldText },
        { juce::TextEditor::highlightColourId,        colour::textSelection },
        { juce::TextEditor::highlightedTextColourId,  textStrong },
        { juce::TextEditor::outlineColourId,          fieldBorder },
        { juce::TextEditor::focusedOutlineColourId,   accent },
        { juce::TextEditor::shadowColourId,           0 },
        { juce::CaretComponent::caretColourId,        textStrong },
        { juce::ScrollBar::thumbColourId,             scrollThumb },
        { juce::ScrollBar::trackColourId,             0 },
        { juce::ScrollBar::backgroundColourId,        0 },
        { juce::Slider::thumbColourId,                thumbTop },
        { juce::Slider::trackColourId,                sliderFill },
        { juce::Slider::backgroundColourId,           sliderTrack },
        { juce::Slider::rotarySliderFillColourId,     accent },
        { juce::Slider::rotarySliderOutlineColourId,  tickMark },
        { juce::Slider::textBoxTextColourId,          fieldText },
        { juce::Slider::textBoxBackgroundColourId,    fieldFill },
        { juce::Slider::textBoxOutlineColourId,       fieldBorder },
        { juce::Slider::textBoxHighlightColourId,     colour::textSelection },
        { juce::TooltipWindow::backgroundColourId,    tooltip },
        { juce::TooltipWindow::textColourId,          colour::text },
        { juce::TooltipWindow::outlineColourId,       paneBorder },
        { juce::AlertWindow::backgroundColourId,      dialog },
        { juce::AlertWindow::textColourId,            colour::text },
        { juce::AlertWindow::outlineColourId,         paneBorder },
        { juce::GroupComponent::outlineColourId,      paneBorder },
        { juce::GroupComponent::textColourId,         textMuted },
        { juce::DirectoryContentsDisplayComponent::highlightColourId, selectedRow },
        { juce::DirectoryContentsDisplayComponent::textColourId,      colour::text },
    };

    for (auto [id, argb] : colourIds)
        setColour (id, juce::Colour (argb));

    getCurrentColourScheme().setUIColour (ColourScheme::windowBackground, c (window));
    getCurrentColourScheme().setUIColour (ColourScheme::widgetBackground, c (pane));
    getCurrentColourScheme().setUIColour (ColourScheme::menuBackground, c (menu));
    getCurrentColourScheme().setUIColour (ColourScheme::outline, c (paneBorder));
    getCurrentColourScheme().setUIColour (ColourScheme::defaultText, c (colour::text));
    getCurrentColourScheme().setUIColour (ColourScheme::defaultFill, c (buttonTop));
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedText, c (textStrong));
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedFill, c (selectedRow));
    getCurrentColourScheme().setUIColour (ColourScheme::menuText, c (colour::text));
}

EncoreLookAndFeel::~EncoreLookAndFeel() = default;

//==============================================================================
juce::Typeface::Ptr EncoreLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Fonts made without a typeface (FontOptions (size)) are Inter too.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    {
        const auto style = f.getTypefaceStyle();
        const auto weight = style.containsIgnoreCase ("extra") ? type::Weight::extraBold
                          : style.containsIgnoreCase ("semi")  ? type::Weight::semibold
                          : style.containsIgnoreCase ("medium") ? type::Weight::medium
                          : f.isBold()                          ? type::Weight::bold
                                                                : type::Weight::regular;
        return EncoreFonts::get().typeface (weight);
    }

    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font EncoreLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

//==============================================================================
juce::Font EncoreLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto v = effectiveVariant (button);

    if (v == Variant::small || buttonHeight <= size::buttonSmallHeight)
        return font (type::buttonSmall);

    if (v == Variant::danger || v == Variant::muted)
        return font (type::buttonBold);

    return font (type::button);
}

int EncoreLookAndFeel::getTextButtonWidthToFitText (juce::TextButton& b, int buttonHeight)
{
    auto w = juce::roundToInt (textWidth (getTextButtonFont (b, buttonHeight), b.getButtonText())) + size::buttonPadX * 2 + 2;

    if (const auto icon = buttonIcon (b))
        w += juce::roundToInt (std::get<1> (*icon)) + iconGap;

    if (const auto key = b.getProperties()["encoreKeyCap"].toString(); key.isNotEmpty())
        w += kit::KeyCap::widthFor (key) + size::keyCapGap;

    return w;
}

void EncoreLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                              bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat();
    const auto segment = getSegment (button);
    const auto variant = effectiveVariant (button);
    const auto enabled = button.isEnabled();
    const auto radius = (float) size::buttonRadius;
    const auto lighten = (highlighted && enabled && ! down) ? state::hoverLighten : 0.0f;
    const auto on = button.getToggleState() || down;

    // A button that looks like a popup field (the patch picker): inset, no drop line.
    if (variant == Variant::field)
    {
        paint::inset (g, r, colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);
        return;
    }

    // Leave room for the 1 px drop line under raised buttons.
    if (segment == Segment::none)
        r = r.withTrimmedBottom (1.0f);

    if (! enabled)
        g.beginTransparencyLayer (state::disabledAlpha);

    if (segment != Segment::none)
    {
        // Joined segments: square inner edges, dividers drawn by the borders.
        const auto shape = segmentShape (r, radius + 1.0f, segment);
        const juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (shape);

        if (on)
            paint::pressed (g, r.expanded (1.0f, 0.0f), 0.0f);
        else
            paint::raised (g, r.expanded (1.0f, 0.0f), colour::buttonTop, colour::buttonBot, colour::buttonBorder, 0.0f, false, lighten);

        g.setColour (c (colour::buttonBorder));
        g.strokePath (shape, juce::PathStrokeType (2.0f));
    }
    else if (variant == Variant::primary)
    {
        paint::raised (g, r, colour::primaryTop, colour::primaryBot, colour::primaryBorder, radius, true, down ? 0.0f : lighten);
    }
    else if (variant == Variant::muted && button.getToggleState())
    {
        paint::raised (g, r, colour::mutedTop, colour::mutedBot, colour::mutedBorder, radius, true, lighten);
    }
    else if (on)
    {
        paint::pressed (g, r, radius);
    }
    else
    {
        paint::raised (g, r, colour::buttonTop, colour::buttonBot, colour::buttonBorder, radius, true, lighten);
    }

    if (! enabled)
        g.endTransparencyLayer();

    // Keyboard focus only (decision 0025: clicks never take focus).
    if (button.hasKeyboardFocus (false) && button.isShowing() && button.getWantsKeyboardFocus())
        paint::focusRing (g, r, radius);
}

void EncoreLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto variant = effectiveVariant (button);

    if (variant == Variant::field)
    {
        // Text left, chevron right, as a combo box field.
        auto area = button.getLocalBounds().toFloat().reduced ((float) size::fieldPadX, 0.0f);
        const auto alpha = button.isEnabled() ? 1.0f : state::disabledAlpha;
        drawIcon (g, Icon::chevronDown, area.removeFromRight ((float) size::fieldChevron).withSizeKeepingCentre ((float) size::fieldChevron, (float) size::fieldChevron),
                  c (colour::textSecondary).withMultipliedAlpha (alpha));
        area.removeFromRight ((float) size::outGap);
        g.setFont (font (type::bodySmall));
        g.setColour (c (colour::fieldText).withMultipliedAlpha (alpha));
        g.drawText (button.getButtonText(), area, juce::Justification::centredLeft, true);
        return;
    }

    const auto on = button.getToggleState();
    auto colourToUse = c (colour::text);

    if (variant == Variant::primary || (variant == Variant::muted && on) || (on && getSegment (button) != Segment::none))
        colourToUse = c (colour::textStrong);
    else if (variant == Variant::danger || (variant == Variant::muted && ! on))
        colourToUse = c (colour::danger);
    else if (on)
        colourToUse = c (colour::textStrong);

    if (! button.isEnabled())
        colourToUse = colourToUse.withMultipliedAlpha (state::disabledAlpha);

    const auto f = getTextButtonFont (button, button.getHeight());
    const auto area = button.getLocalBounds().withTrimmedBottom (getSegment (button) == Segment::none ? 1 : 0).reduced (6, 0);
    g.setFont (f);
    g.setColour (colourToUse);

    if (const auto key = button.getProperties()["encoreKeyCap"].toString(); key.isNotEmpty())
    {
        // Text and key cap centred together.
        const auto capW = (float) kit::KeyCap::widthFor (key);
        const auto textW = juce::jmin ((float) area.getWidth() - capW - (float) size::keyCapGap, textWidth (f, button.getButtonText()) + 1.0f);
        auto row = area.toFloat().withSizeKeepingCentre (juce::jmin (textW + (float) size::keyCapGap + capW, (float) area.getWidth()), (float) area.getHeight());
        const auto cap = row.removeFromRight (capW).withSizeKeepingCentre (capW, (float) size::keyCapH);
        kit::paintKeyCap (g, cap, key);
        g.setFont (f);
        g.setColour (colourToUse);
        g.drawText (button.getButtonText(), row.withTrimmedRight ((float) size::keyCapGap), juce::Justification::centred, true);
        return;
    }

    if (const auto icon = buttonIcon (button))
    {
        // Text and icon centred together (single line).
        const auto [which, iconSize, after] = *icon;
        const auto hasText = button.getButtonText().isNotEmpty();   // icon-only buttons centre the icon
        const auto gapW = hasText ? (float) iconGap : 0.0f;
        const auto textW = hasText ? juce::jmin ((float) area.getWidth() - iconSize - gapW, textWidth (f, button.getButtonText()) + 1.0f) : 0.0f;
        const auto groupW = textW + gapW + iconSize;
        auto row = area.toFloat().withSizeKeepingCentre (juce::jmin (groupW, (float) area.getWidth()), (float) area.getHeight());
        const auto iconBox = (after ? row.removeFromRight (iconSize) : row.removeFromLeft (iconSize))
                                 .withSizeKeepingCentre (iconSize, iconSize);
        drawIcon (g, which, iconBox, colourToUse);

        if (hasText)
            g.drawText (button.getButtonText(), after ? row.withTrimmedRight (gapW) : row.withTrimmedLeft (gapW),
                        juce::Justification::centred, true);
        return;
    }

    g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 2, 0.9f);
}

void EncoreLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    constexpr float box = 13.0f, gap = 6.0f;
    const auto bounds = button.getLocalBounds().toFloat();
    drawTickBox (g, button, 0.0f, (bounds.getHeight() - box) * 0.5f, box, box,
                 button.getToggleState(), button.isEnabled(), highlighted, down);

    auto textColour = c (colour::text);

    if (! button.isEnabled())
        textColour = c (colour::textTertiary);

    g.setColour (textColour);
    g.setFont (font (type::body));
    g.drawFittedText (button.getButtonText(), bounds.withTrimmedLeft (box + gap).toNearestInt(),
                      juce::Justification::centredLeft, 2, 0.9f);
}

void EncoreLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                                     bool ticked, bool enabled, bool, bool)
{
    const juce::Rectangle<float> r (x, y, w, h);

    if (ticked)
    {
        g.setColour (c (colour::accent).withMultipliedAlpha (enabled ? 1.0f : state::disabledAlpha));
        g.fillRoundedRectangle (r, 3.0f);
        paint::tick (g, r, c (colour::textStrong), 1.6f);
    }
    else
    {
        g.setColour (c (colour::checkboxFill));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (c (colour::checkboxBorder).withMultipliedAlpha (enabled ? 1.0f : state::disabledAlpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
}

//==============================================================================
void EncoreLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const auto enabled = box.isEnabled();

    if (! enabled)
        g.beginTransparencyLayer (state::disabledAlpha);

    if (getVariant (box) == Variant::field)
    {
        paint::inset (g, r, colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);
    }
    else
    {
        r = r.withTrimmedBottom (1.0f);

        if (down || box.isPopupActive())
            paint::pressed (g, r, (float) size::buttonRadius);
        else
            paint::raised (g, r, colour::buttonTop, colour::buttonBot, colour::buttonBorder, (float) size::buttonRadius, true,
                           box.isMouseOver (true) ? state::hoverLighten : 0.0f);
    }

    drawIcon (g, Icon::chevronDown, juce::Rectangle<float> (r.getRight() - 19.0f, r.getCentreY() - 4.5f, 9.0f, 9.0f),
              c (colour::textSecondary));

    if (! enabled)
        g.endTransparencyLayer();
}

juce::Font EncoreLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return font (type::bodySmall);
}

void EncoreLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setBorderSize ({});
    label.setColour (juce::Label::textColourId, c (getVariant (box) == Variant::field ? colour::fieldText : colour::text));
}

//==============================================================================
void EncoreLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options&)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const auto radius = (float) size::menuRadius;
    g.setColour (c (colour::menu));
    g.fillRoundedRectangle (r, radius);
    g.setColour (c (colour::menuHighlightBorder));
    paint::hline (g, r.getX() + radius, 1.0f, r.getWidth() - radius * 2.0f);
    g.setColour (c (colour::paneBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void EncoreLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted,
                                                      const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    if (item.isSeparator)
    {
        g.setColour (c (colour::menuSeparator));
        g.fillRect (area.getX() + menuTickInset, area.getCentreY(), area.getWidth() - menuTickInset * 2, 1);
        return;
    }

    const auto r = area.toFloat();
    const auto active = item.isEnabled;
    const auto lit = highlighted && active;
    const auto hasSubMenu = item.subMenu != nullptr && (item.itemID == 0 || item.subMenu->getNumItems() > 0);

    if (lit)
    {
        g.setColour (c (colour::menuHighlight));
        g.fillRoundedRectangle (r, 4.0f);
    }

    auto textColour = c (colour::text);

    if (item.colour != juce::Colour())
        textColour = item.colour;   // danger items (Delete this layout)

    if (lit)
        textColour = c (colour::textStrong);

    if (! active)
        textColour = c (colour::textDisabled);

    if (item.isTicked)
        paint::tick (g, juce::Rectangle<float> (r.getX() + (float) menuTickInset, r.getCentreY() - 6.0f, 12.0f, 12.0f),
                     textColour, 1.5f);

    auto [label, shortcut] = item.shortcutKeyDescription.isNotEmpty() ? std::pair { item.text, item.shortcutKeyDescription }
                                                                       : splitShortcut (item.text);

    auto textArea = r.withTrimmedLeft ((float) menuTextIndent).withTrimmedRight ((float) menuRightPad);

    if (hasSubMenu)
    {
        drawIcon (g, Icon::triangleRight, textArea.removeFromRight ((float) menuArrowWidth + 4.0f).withSizeKeepingCentre (10.0f, 10.0f),
                  c (lit ? colour::selectedRowSub : colour::menuShortcut));
    }
    else if (shortcut.isNotEmpty())
    {
        const auto shortcutFont = font (type::menuShortcut);
        const auto w = textWidth (shortcutFont, shortcut);
        g.setFont (shortcutFont);
        g.setColour (c (lit ? colour::selectedRowSub : colour::menuShortcut).withMultipliedAlpha (active ? 1.0f : 0.6f));
        g.drawText (shortcut, textArea.removeFromRight (w + 1.0f), juce::Justification::centredRight, false);
        textArea.removeFromRight ((float) menuShortcutGap * 0.5f);
    }

    g.setFont (getPopupMenuFont());
    g.setColour (textColour);
    g.drawText (label, textArea, juce::Justification::centredLeft, true);
}

void EncoreLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                               const juce::String& name, const juce::PopupMenu::Options&)
{
    g.setFont (font (type::paneHeader));
    g.setColour (c (colour::textDisabled));
    g.drawText (name.toUpperCase(), area.toFloat().withTrimmedLeft ((float) menuTickInset + 4.0f).withTrimmedBottom (3.0f),
                juce::Justification::bottomLeft, true);
}

void EncoreLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int,
                                                              int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = size::menuSeparatorH;
        return;
    }

    auto [label, shortcut] = splitShortcut (text);
    auto w = textWidth (getPopupMenuFont(), label) + (float) (menuTextIndent + menuRightPad + menuArrowWidth + 18);

    if (shortcut.isNotEmpty())
        w += textWidth (font (type::menuShortcut), shortcut) + (float) menuShortcutGap;

    idealWidth = juce::roundToInt (w);
    idealHeight = size::menuItemHeight;
}

void EncoreLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& idealWidth,
                                                                       int& idealHeight, const juce::PopupMenu::Options&)
{
    idealWidth = juce::roundToInt (textWidth (font (type::paneHeader), text.toUpperCase())) + 24;
    idealHeight = menuHeaderHeight;
}

juce::Font EncoreLookAndFeel::getPopupMenuFont()
{
    return font (type::menuItem);
}

int EncoreLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return size::menuPadding;
}

void EncoreLookAndFeel::drawPopupMenuUpDownArrowWithOptions (juce::Graphics& g, int width, int height, bool isScrollUpArrow,
                                                             const juce::PopupMenu::Options&)
{
    drawIcon (g, isScrollUpArrow ? Icon::chevronUp : Icon::chevronDown,
              juce::Rectangle<float> ((float) width, (float) height).withSizeKeepingCentre (10.0f, 10.0f),
              c (colour::menuShortcut));
}

//==============================================================================
int EncoreLookAndFeel::getDefaultScrollbarWidth()  { return size::scrollbar; }
bool EncoreLookAndFeel::areScrollbarButtonsVisible() { return false; }

void EncoreLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                       int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    if (thumbSize <= 0)
        return;

    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (c ((mouseOver || mouseDown) ? colour::scrollThumbHover : colour::scrollThumb));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

//==============================================================================
int EncoreLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    return s.isRotary() ? 0 : 6;
}

juce::Label* EncoreLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    // The value box is an inset field with the value right-aligned
    // (the Out volume: "−12.0 dB").
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    setVariant (*label, Variant::field);
    label->setFont (font (type::bodySmall));
    label->setJustificationType (juce::Justification::centredRight);
    // Right-aligned with 8 px on the right; on the left the text may use the
    // padding (the board's "−12.0 dB" is wider than 66 − 2 × 8).
    label->setBorderSize ({ 0, size::fieldPadX / 4, 0, size::fieldPadX });
    label->setColour (juce::Label::textColourId, c (colour::fieldText));
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::textColourId, c (colour::fieldText));
    label->setColour (juce::TextEditor::backgroundColourId, c (colour::fieldFill));
    label->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::focusedOutlineColourId, c (colour::accent));
    label->setColour (juce::TextEditor::highlightColourId, c (colour::textSelection));
    return label;
}

void EncoreLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (getVariant (label) != Variant::field)
    {
        juce::LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    paint::inset (g, label.getLocalBounds().toFloat(), colour::fieldFill, colour::fieldBorder, (float) size::fieldRadius, colour::fieldShadow);

    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : state::disabledAlpha;
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (getLabelFont (label));
        // Never an ellipsis in a number: squeeze it a little instead.
        g.drawFittedText (label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()),
                          label.getJustificationType(), 1, 0.8f);
    }
}

void EncoreLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0, 0, style, slider);
        return;
    }

    const auto cy = (float) y + (float) height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
    g.setColour (c (colour::sliderTrack));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (c (colour::sliderFill));
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);

    const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy });
    g.setGradientFill (juce::ColourGradient::vertical (c (colour::thumbTop), thumb.getY(), c (colour::thumbBot), thumb.getBottom()));
    g.fillEllipse (thumb);
    g.setColour (c (colour::buttonBorder).withAlpha (0.6f));
    g.drawEllipse (thumb.reduced (0.5f), 1.0f);
}

void EncoreLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> cell, float proportion, bool enabled)
{
    // 11 ticks on a 270° arc (radius 25…30 of a 64 cell; majors at both ends
    // and the top, 1.6 px; minors 1 px), a 46/64 knob, a 2 × 16 pointer
    // (DESIGN-SYSTEM.md §8). Disabled: every colour at 45 % (no layer).
    const auto alpha = enabled ? 1.0f : state::disabledAlpha;
    const auto side = juce::jmin (cell.getWidth(), cell.getHeight());
    const auto area = juce::Rectangle<float> (side, side).withCentre (cell.getCentre());
    const auto centre = area.getCentre();
    const auto scale = side / 64.0f;
    const auto startAngle = -juce::MathConstants<float>::pi * 0.75f;
    const auto range = juce::MathConstants<float>::pi * 1.5f;

    g.setColour (c (colour::tickMark).withMultipliedAlpha (alpha));

    for (int i = 0; i <= 10; ++i)
    {
        const auto angle = startAngle + range * (float) i / 10.0f;
        const auto major = i == 0 || i == 5 || i == 10;
        const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
        g.drawLine ({ centre + dir * (25.0f * scale), centre + dir * (30.0f * scale) }, (major ? 1.6f : 1.0f) * scale);
    }

    const auto knob = juce::Rectangle<float> (46.0f * scale, 46.0f * scale).withCentre (centre);

    // box-shadow 0 2px 3px black 50 %, approximated by two soft rings.
    g.setColour (c (colour::knobShadow).withMultipliedAlpha (alpha * 0.5f));
    g.fillEllipse (knob.translated (0.0f, 2.0f * scale).expanded (1.0f * scale));
    g.setColour (c (colour::knobShadow).withMultipliedAlpha (alpha));
    g.fillEllipse (knob.translated (0.0f, 2.0f * scale));

    // radial-gradient (circle at 40 % 35 %, top, bottom 70 %)
    const juce::Point<float> light (knob.getX() + knob.getWidth() * 0.4f, knob.getY() + knob.getHeight() * 0.35f);
    const auto reach = light.getDistanceFrom (knob.getBottomRight()) * 0.7f;
    g.setGradientFill (juce::ColourGradient (c (colour::knobTop).withMultipliedAlpha (alpha), light,
                                             c (colour::knobBot).withMultipliedAlpha (alpha), light.translated (reach, 0.0f), true));
    g.fillEllipse (knob);
    g.setColour (c (colour::knobBorder).withMultipliedAlpha (alpha));
    g.drawEllipse (knob.reduced (0.5f), 1.0f);

    const auto angle = startAngle + range * juce::jlimit (0.0f, 1.0f, proportion);
    // The pointer at 64-cell scale, pointing up from the centre; made once.
    static const juce::Path pointer = []
    {
        juce::Path p;
        p.addRoundedRectangle (-1.0f, -21.0f, 2.0f, 16.0f, 1.0f);
        return p;
    }();

    g.setColour (c (colour::knobPointer).withMultipliedAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::scale (scale).rotated (angle).translated (centre));
}

void EncoreLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float proportion, float, float,
                                          juce::Slider& slider)
{
    drawKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), proportion, slider.isEnabled());
}

//==============================================================================
void EncoreLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r, (float) size::fieldRadius);
}

void EncoreLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
    {
        g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), (float) size::fieldRadius, 1.5f);
    }
    else
    {
        g.setColour (editor.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), (float) size::fieldRadius, 1.0f);
    }
}

//==============================================================================
juce::Rectangle<int> EncoreLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto f = font (type::label);
    juce::AttributedString s;
    s.append (text, f, c (colour::text));
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 360.0f);

    const auto w = (int) std::ceil (layout.getWidth()) + tooltipPadX * 2;
    const auto h = (int) std::ceil (layout.getHeight()) + tooltipPadY * 2;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
               .constrainedWithin (parentArea);
}

void EncoreLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (c (colour::tooltip));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (c (colour::paneBorder));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, font (type::label), c (colour::text));
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, (float) width - tooltipPadX * 2);
    layout.draw (g, r.reduced ((float) tooltipPadX, (float) tooltipPadY));
}

//==============================================================================
void EncoreLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                                      juce::TextLayout& textLayout)
{
    // DialogFrame look at a compact size: body, title bar, footer behind the buttons.
    const auto bounds = alert.getLocalBounds().toFloat();
    const auto radius = (float) size::dialogRadius;
    g.setColour (c (colour::dialog));
    g.fillRoundedRectangle (bounds, radius);

    {
        juce::Path bar;
        bar.addRoundedRectangle (0.0f, 0.0f, bounds.getWidth(), (float) alertTitleBar, radius, radius, true, true, false, false);
        g.setGradientFill (juce::ColourGradient::vertical (c (colour::dialogTitleTop), 0.0f, c (colour::dialogTitleBot), (float) alertTitleBar));
        g.fillPath (bar);
        g.setColour (c (colour::paneBorder));
        paint::hline (g, 0.0f, (float) alertTitleBar - 1.0f, bounds.getWidth());
        paint::text (g, alert.getName(), juce::Rectangle<float> (0.0f, 0.0f, bounds.getWidth(), (float) alertTitleBar),
                     type::dialogTitle, colour::textSoft, juce::Justification::centred);
    }

    // Footer behind the buttons.
    auto buttonsTop = bounds.getBottom();

    for (auto* child : alert.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child))
            buttonsTop = juce::jmin (buttonsTop, (float) b->getY());

    if (buttonsTop < bounds.getBottom())
    {
        const auto footerTop = buttonsTop - 12.0f;
        juce::Path footer;
        footer.addRoundedRectangle (0.0f, footerTop, bounds.getWidth(), bounds.getBottom() - footerTop, radius, radius, false, false, true, true);
        g.setColour (c (colour::dialogFooter));
        g.fillPath (footer);
        g.setColour (c (colour::paneBorder));
        paint::hline (g, 0.0f, footerTop, bounds.getWidth());
    }

    // The message. JUCE lays the title out as the first line of the text; it's
    // in the title bar already, so that line is clipped away.
    {
        const juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, alertTitleBar, alert.getWidth(), alert.getHeight() - alertTitleBar));
        textLayout.draw (g, textArea.toFloat());
    }

    g.setColour (c (colour::paneBorder));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

int EncoreLookAndFeel::getAlertWindowButtonHeight()  { return size::buttonHeight; }
juce::Font EncoreLookAndFeel::getAlertWindowTitleFont()   { return fontOfSize (type::Weight::semibold, 13.0f); }
juce::Font EncoreLookAndFeel::getAlertWindowMessageFont() { return font (type::body); }
juce::Font EncoreLookAndFeel::getAlertWindowFont()        { return font (type::bodySmall); }

} // namespace encore::theme
