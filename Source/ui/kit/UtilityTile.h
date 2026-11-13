// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "ui/theme/Icons.h"

#include <vector>

namespace encore::kit
{

/** What a utility tile shows (NEW-FEATURES-M10-M11 §C). */
struct UtilityTileInfo
{
    enum class Dot { none, on, off, amber };

    juce::String label;     // VST3, IN, OUT, SPLIT (drawn in capitals)
    juce::String name;      // up to two lines, ellipsised
    juce::String caption;   // the maker, a channel
    Dot dot = Dot::none;
    bool amber = false;     // a problem: amber border and name
    bool footswitch = false;

    /** Jack and split / merge tiles: the caption, then the name, at the
        bottom (IN: "input 1" over "Guitar"; SPLIT: "A + B"). */
    bool nameAtBottom = false;

    /** Jack and split / merge tiles: an icon in the middle (22 px). */
    std::optional<theme::Icon> icon;

    /** The OUT tile: a line per send (the output's name, a small bar on the
        send law, or M when that output is muted), then "+ N more". */
    struct Line
    {
        juce::String text;
        float proportion = 0.0f;   // of the bar, 0 … 1
        bool muted = false;

        bool operator== (const Line&) const = default;
    };

    std::vector<Line> lines;
    juce::String more;

    bool operator== (const UtilityTileInfo& o) const
    {
        return label == o.label && name == o.name && caption == o.caption && dot == o.dot && amber == o.amber
            && footswitch == o.footswitch && nameAtBottom == o.nameAtBottom && icon == o.icon && lines == o.lines && more == o.more;
    }

    bool operator!= (const UtilityTileInfo& o) const { return ! (*this == o); }
};

/** A chain tile that isn't gear (the plugin tile, later the IN / OUT jacks
    and the split and merge tiles): 84 high, radius 8, #2C2C30 → #1F1F22,
    border #3A3A3F; label top left, status dot top right (not on tiles
    narrower than 60), the name in up to two lines, a caption, and an
    optional small footswitch. Paints only; the owner handles clicks and
    draws the selection outline, as for gear tiles. Its drop shadow falls
    outside it: the owner paints it underneath with paintShadow(). */
class UtilityTile final : public juce::Component
{
public:
    explicit UtilityTile (const UtilityTileInfo& = {});

    void setInfo (const UtilityTileInfo&);
    const UtilityTileInfo& getInfo() const noexcept { return info; }

    /** The tile's shadow (0, 6, 6 px, black 45 %) under a tile whose
        bounds are `tileBounds` in the owner's coordinates (cached image). */
    static void paintShadow (juce::Graphics&, juce::Rectangle<float> tileBounds);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void update();   // the name's lines, when the text or the width changes

    UtilityTileInfo info;
    juce::String labelCaps;
    juce::StringArray nameLines;
};

} // namespace encore::kit
