// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/theme/EncoreLookAndFeel.h"

namespace encore::test
{

/** The app's look as the default while a UI test runs (the app sets it in Main.cpp). */
struct ScopedEncoreLook
{
    ScopedEncoreLook()  { juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); }
    ~ScopedEncoreLook() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

    theme::EncoreLookAndFeel lookAndFeel;
};

/** Renders a component off-screen; with ENCORE_UI_SNAPSHOTS set, also writes
    <folder>/<name>.png (the names match docs/design/reference/target). */
inline juce::Image renderSnapshot (juce::Component& c, const juce::String& name, float scale = 1.0f)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), false, scale);

    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ENCORE_UI_SNAPSHOTS", {}); folder.isNotEmpty())
    {
        juce::File (folder).createDirectory();
        const auto file = juce::File (folder).getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    return image;
}

/** Something brighter than the background was drawn. */
inline bool hasContent (const juce::Image& image)
{
    for (int y = 0; y < image.getHeight(); y += 7)
        for (int x = 0; x < image.getWidth(); x += 7)
            if (image.getPixelAt (x, y).getBrightness() > 0.6f)
                return true;

    return false;
}

} // namespace encore::test
