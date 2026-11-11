// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/theme/Icons.h"

#include "EncoreUiAssets.h"

namespace encore::theme
{

namespace
{
    constexpr int numIcons = (int) Icon::triangleLeft + 1;

    juce::Path outlineFromSvg (const char* data, int size)
    {
        // The icons draw in "currentColor"; any solid colour gives the same outline.
        auto text = juce::String::fromUTF8 (data, size).replace ("currentColor", "#000000");

        if (auto drawable = juce::Drawable::createFromSVGString (text))
            return drawable->getOutlineAsPath();

        jassertfalse;   // a broken icon file
        return {};
    }

    juce::Path triangle (bool right)
    {
        // A small solid play-style triangle, like ▸ / ◂ in the designs.
        juce::Path p;

        if (right)
            p.addTriangle (8.0f, 5.0f, 8.0f, 19.0f, 19.0f, 12.0f);
        else
            p.addTriangle (16.0f, 5.0f, 16.0f, 19.0f, 5.0f, 12.0f);

        return p;
    }

    struct IconSet
    {
        std::array<juce::Path, (size_t) numIcons> paths;

        IconSet()
        {
            using namespace EncoreUiAssets;
            const std::pair<Icon, std::pair<const char*, int>> files[] = {
                { Icon::alert,        { alert_svg,        alert_svgSize } },
                { Icon::arrowRight,   { arrowright_svg,   arrowright_svgSize } },
                { Icon::audioWave,    { audiowave_svg,    audiowave_svgSize } },
                { Icon::check,        { check_svg,        check_svgSize } },
                { Icon::chevronDown,  { chevrondown_svg,  chevrondown_svgSize } },
                { Icon::chevronUp,    { chevronup_svg,    chevronup_svgSize } },
                { Icon::close,        { close_svg,        close_svgSize } },
                { Icon::duplicate,    { duplicate_svg,    duplicate_svgSize } },
                { Icon::lock,         { lock_svg,         lock_svgSize } },
                { Icon::midiPort,     { midiport_svg,     midiport_svgSize } },
                { Icon::minus,        { minus_svg,        minus_svgSize } },
                { Icon::plus,         { plus_svg,         plus_svgSize } },
                { Icon::sidebarRight, { sidebarright_svg, sidebarright_svgSize } },
                { Icon::sliders,      { sliders_svg,      sliders_svgSize } },
                { Icon::trash,        { trash_svg,        trash_svgSize } },
            };

            for (auto& [icon, file] : files)
                paths[(size_t) icon] = outlineFromSvg (file.first, file.second);

            paths[(size_t) Icon::triangleRight] = triangle (true);
            paths[(size_t) Icon::triangleLeft] = triangle (false);
        }
    };

    const IconSet& icons()
    {
        static const IconSet set;   // built on first use, on the message thread
        return set;
    }

    struct Logo
    {
        std::unique_ptr<juce::Drawable> drawable;
        juce::Rectangle<float> box;

        Logo()
        {
            using namespace EncoreUiAssets;

            const auto svg = juce::String::fromUTF8 (encorewordmarkui_svg, encorewordmarkui_svgSize);
            drawable = juce::Drawable::createFromSVGString (svg);

            // Place it by its viewBox (the designed 98 x 17 box), not by its
            // ink: the parsed drawable reports the viewBox in its own
            // coordinates (JUCE may move the origin), so ask it.
            if (drawable != nullptr)
                box = drawable->getContentBounds();

            if (box.isEmpty() && drawable != nullptr)
                box = drawable->getDrawableBounds();
        }
    };

    const Logo& logo()
    {
        static const Logo l;
        return l;
    }
}

const juce::Path& iconPath (Icon icon)
{
    return icons().paths[(size_t) icon];
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());

    if (side <= 0.0f)
        return;

    const auto box = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    g.setColour (colour);
    g.fillPath (iconPath (icon), juce::AffineTransform::scale (side / 24.0f).translated (box.getX(), box.getY()));
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& l = logo();

    if (l.drawable != nullptr && ! l.box.isEmpty())
        l.drawable->draw (g, 1.0f, juce::RectanglePlacement (juce::RectanglePlacement::centred).getTransformToFit (l.box, bounds));
}

float logoAspect()
{
    const auto& l = logo();
    return l.box.getHeight() > 0.0f ? l.box.getWidth() / l.box.getHeight() : 98.0f / 17.0f;
}

} // namespace encore::theme
