// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/DragImage.h"

#include "ui/theme/EncoreTheme.h"

namespace encore::kit
{

DragImage::DragImage()
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
}

void DragImage::start (juce::Component& source, juce::Rectangle<int> area, juce::Point<int> grab)
{
    const auto scale = juce::Component::getApproximateScaleFactorForComponent (&source);
    image = source.createComponentSnapshot (area, true, scale);
    offset = grab - area.getPosition();
    setSize (area.getWidth(), area.getHeight());
    setVisible (image.isValid());
    toFront (false);
}

void DragImage::moveTo (juce::Point<int> screen)
{
    if (! isDragging() || getParentComponent() == nullptr)
        return;

    setTopLeftPosition (getParentComponent()->getLocalPoint (nullptr, screen) - offset);
    toFront (false);
}

void DragImage::stop()
{
    image = {};
    setVisible (false);
}

void DragImage::paint (juce::Graphics& g)
{
    if (image.isValid())
    {
        g.setOpacity (theme::state::dragImageAlpha);
        g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }
}

} // namespace encore::kit
