// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/Group.h"

#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

Group::Group (const juce::String& t) : title (paint::caps (type::groupTitle, t))
{
    setInterceptsMouseClicks (false, true);
}

juce::Rectangle<int> Group::getTitleBounds() const { return getLocalBounds().removeFromTop (size::groupTitleH); }
juce::Rectangle<int> Group::getBoxBounds() const   { return getLocalBounds().withTrimmedTop (size::groupTitleH); }
int Group::heightFor (int boxHeight) noexcept      { return size::groupTitleH + boxHeight; }

void Group::paint (juce::Graphics& g)
{
    paint::text (g, title, getTitleBounds().toFloat(), type::groupTitle, colour::textMuted, juce::Justification::topLeft);

    const auto box = getBoxBounds().toFloat();
    g.setColour (paint::c (colour::groupBox));
    g.fillRoundedRectangle (box, (float) size::groupRadius);
    g.setColour (paint::c (colour::paneBorder));
    g.drawRoundedRectangle (box.reduced (0.5f), (float) size::groupRadius, 1.0f);
}

} // namespace encore::kit
