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

/** With ENCORE_UI_SNAPSHOTS set, writes <folder>/<name>.png. */
inline void writeSnapshot (const juce::Image& image, const juce::String& name)
{
    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ENCORE_UI_SNAPSHOTS", {}); folder.isNotEmpty())
    {
        juce::File (folder).createDirectory();
        const auto file = juce::File (folder).getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
}

/** Renders a component off-screen; with ENCORE_UI_SNAPSHOTS set, also writes
    <folder>/<name>.png (the names match docs/design/reference/target). */
inline juce::Image renderSnapshot (juce::Component& c, const juce::String& name, float scale = 1.0f)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), false, scale);
    writeSnapshot (image, name);
    return image;
}

/** Draws a popup menu with the look and feel, as the real (desktop window)
    menu would draw it, at `at`; returns each item's area. Menus are separate
    windows, so a snapshot of the main window can't contain them. */
inline std::vector<juce::Rectangle<int>> paintMenu (juce::Graphics& g, juce::LookAndFeel& lnf, const juce::PopupMenu& menu,
                                                    juce::Point<int> at, int highlighted = -1)
{
    const juce::PopupMenu::Options options;
    const auto border = lnf.getPopupMenuBorderSizeWithOptions (options);
    std::vector<juce::PopupMenu::Item> items;
    std::vector<juce::Point<int>> sizes;
    int width = 0, height = 0;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        int w = 0, h = 0;

        if (item.isSectionHeader)
            lnf.getIdealPopupMenuSectionHeaderSizeWithOptions (item.text, -1, w, h, options);
        else
            lnf.getIdealPopupMenuItemSizeWithOptions (item.text, item.isSeparator, -1, w, h, options);

        items.push_back (item);
        sizes.push_back ({ w, h });
        width = juce::jmax (width, w);
        height += h;
    }

    const auto menuW = width + 2 * border, menuH = height + 2 * border;
    std::vector<juce::Rectangle<int>> areas;
    const juce::Graphics::ScopedSaveState state (g);
    g.setOrigin (at);
    lnf.drawPopupMenuBackgroundWithOptions (g, menuW, menuH, options);
    auto y = border;

    for (size_t i = 0; i < items.size(); ++i)
    {
        const juce::Rectangle<int> area (border, y, width, sizes[i].y);

        if (items[i].isSectionHeader)
            lnf.drawPopupMenuSectionHeaderWithOptions (g, area, items[i].text, options);
        else
            lnf.drawPopupMenuItemWithOptions (g, area, (int) i == highlighted, items[i], options);

        areas.push_back (area + at);
        y += sizes[i].y;
    }

    return areas;
}

/** Something brighter than the background was drawn. */
inline bool hasContent (const juce::Image& image)
{
    // Every pixel of small images (a 14 px meter: text is easily missed by a
    // sparse grid, and font rendering differs between platforms).
    const auto step = image.getWidth() * image.getHeight() > 400000 ? 3 : 1;

    for (int y = 0; y < image.getHeight(); y += step)
        for (int x = 0; x < image.getWidth(); x += step)
            if (const auto c = image.getPixelAt (x, y); c.getAlpha() > 64 && c.getBrightness() > 0.6f)
                return true;

    return false;
}

} // namespace encore::test
