// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
    bool hasHeader() const noexcept { return headerHeight > 0; }

    /** Space for header actions: the header minus its padding and label. */
    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getContentBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateHeaderShape();

    juce::String label, rightText;   // label already in capitals
    int headerHeight = 0;
    juce::Path headerShape;
    juce::ColourGradient headerFill;
};

/** A section title inside a pane ("PARAMETERS" + right text), with a line
    above it: the inspector's subheaders. Paint helper, no component; pass
    the label through paint::caps (type::paneHeader, ...) when it changes. */
void paintSubheader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& label, const juce::String& rightText);

} // namespace encore::kit
