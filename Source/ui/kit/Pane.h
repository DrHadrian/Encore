// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "ui/theme/EncoreTheme.h"

namespace encore::kit
{

/** A panel: fill, 1 px border, radius 6, top highlight and an optional
    24 px header (label left, optional plain text right). Children are
    placed by the owner: getHeaderBounds() for header actions (icon buttons,
    small buttons), getContentBounds() for the body. */
class Pane : public juce::Component
{
public:
    Pane() = default;

    /** The label is drawn in capitals; the right text as it is. */
    void setHeader (const juce::String& label, const juce::String& rightText = {});
    void setHeaderRightText (const juce::String& rightText);
    void setHeaderHeight (int height);   // 24 (Edit), 22 (Stage)

    /** The block stage's header: another gradient, a status dot before the
        label (none: no dot), room on the right for header buttons. */
    void setHeaderColours (juce::uint32 top, juce::uint32 bottom);
    void setHeaderDot (std::optional<juce::uint32> dotColour);
    void setHeaderRightInset (int px);
    bool hasHeader() const noexcept { return headerHeight > 0; }

    /** Space for header actions: the header minus its padding and label. */
    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getContentBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateHeaderShape();

    juce::String headerText, rightText;   // headerText already in capitals
    int headerHeight = 0, rightInset = 0;
    float rightTextW = 0.0f;
    juce::uint32 headerTop = theme::colour::paneHeaderTop, headerBot = theme::colour::paneHeaderBot;
    std::optional<juce::uint32> headerDot;
    juce::Path headerShape;
    juce::ColourGradient headerFill;
};

/** A section title inside a pane ("PARAMETERS" + right text), with a line
    above it: the inspector's subheaders. Paint helper, no component; pass
    the label through paint::caps (type::paneHeader, ...) when it changes. */
void paintSubheader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& label, const juce::String& rightText,
                     bool lineAbove = true);

} // namespace encore::kit
