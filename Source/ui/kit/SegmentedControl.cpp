// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/SegmentedControl.h"

#include "ui/theme/EncoreLookAndFeel.h"

namespace encore::kit
{

using namespace theme;

void SegmentedControl::setSegments (const juce::StringArray& labels)
{
    if (labels.size() == buttons.size())
    {
        for (int i = 0; i < labels.size(); ++i)
            buttons[i]->setButtonText (labels[i]);

        return;
    }

    buttons.clear();

    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setTitle (labels[i]);
        setSegment (*b, labels.size() == 1 ? Segment::none
                        : i == 0 ? Segment::first
                        : i == labels.size() - 1 ? Segment::last
                                                 : Segment::middle);
        b->onClick = [this, i] { if (onClick != nullptr) onClick (i); };
        addAndMakeVisible (b);
    }

    setSelected (selected);
    resized();
}

void SegmentedControl::setTooltips (const juce::StringArray& tooltips)
{
    for (int i = 0; i < buttons.size() && i < tooltips.size(); ++i)
        buttons[i]->setTooltip (tooltips[i]);
}

void SegmentedControl::setSelected (int index)
{
    selected = index;

    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i]->getToggleState() != (i == index))
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

void SegmentedControl::setSegmentWidths (std::vector<int> newWidths)
{
    widths = std::move (newWidths);
    resized();
}

void SegmentedControl::resized()
{
    if (buttons.isEmpty())
        return;

    // Fixed widths when given (the rest of the width goes to the last), else equal shares.
    if ((int) widths.size() == buttons.size())
    {
        auto x = 0;

        for (int i = 0; i < buttons.size(); ++i)
        {
            const auto w = i == buttons.size() - 1 ? getWidth() - x : widths[(size_t) i];
            buttons[i]->setBounds (x, 0, w, getHeight());
            x += w;
        }

        return;
    }

    const auto w = (float) getWidth() / (float) buttons.size();

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (juce::roundToInt (w * (float) i), 0,
                               juce::roundToInt (w * (float) (i + 1)) - juce::roundToInt (w * (float) i), getHeight());
}

} // namespace encore::kit
