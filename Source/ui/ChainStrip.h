// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/core/Chain.h"
#include "session/ShowSession.h"
#include "ui/kit/GearPlaceholder.h"
#include "ui/kit/UtilityTile.h"
#include "ui/kit/Pane.h"

namespace encore
{

/** The patch's signal chain as a row of tiles, left to right (Helix/MainStage
    style). Click a tile to edit it below; right-click to move, insert or
    remove; the "+" tile adds a block at the end. Message thread only.
    Looks: docs/design/SCREENS.md §4.3 (the Signal chain pane): gear
    placeholders on a board, bottom-aligned, the selected one outlined. */
class ChainStrip final : public kit::Pane
{
public:
    explicit ChainStrip (ShowSession& session);

    /** The live chain to show (null while none). Keeps the selection if possible. */
    void setChain (Chain* chain);
    Chain* getChain() const noexcept { return chain; }

    int getSelectedIndex() const noexcept { return selected; }
    void selectBlock (int index);   // as if clicked
    std::function<void (int)> onSelectionChanged;

    /** "Find plugins..." in the add menu (opens Settings > Plugins). */
    std::function<void()> onFindPlugins;

    /** Call from a UI timer: tile states (on/off, loaded file) can change anywhere. */
    void refresh();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** What a block shows as gear (shape, kind label, name, on). */
    static kit::BlockInfo describe (const Block&);

    /** What a plugin block's tile shows: format, name, maker, its state. */
    static kit::UtilityTileInfo describePlugin (const Block&);

    /** The Add block menu (the "+" tile, Insert before / after): block
        types, then Plugin ▸ makers ▸ plugins, Find plugins... */
    juce::PopupMenu blockTypeMenu (int insertAt);
    juce::Rectangle<int> getAddTileBounds() const noexcept { return addTile; }

private:
    /** A block's tile: gear (a placeholder) or a utility tile (a plugin). */
    struct TileView
    {
        std::unique_ptr<kit::GearPlaceholder> gear;
        std::unique_ptr<kit::UtilityTile> utility;

        juce::Component& component() const { return gear != nullptr ? static_cast<juce::Component&> (*gear) : *utility; }
    };

    void rebuildTiles();
    void updateTile (TileView&, const Block&);
    juce::Rectangle<int> getBoardBounds() const;

    void select (int index);
    void showBlockMenu (int index);
    void showAddMenu (int insertAt);

    ShowSession& session;
    Chain* chain = nullptr;
    int selected = 0;
    std::vector<juce::Rectangle<int>> tiles;
    std::vector<TileView> views;
    juce::Rectangle<int> addTile;
    juce::Path addTileOutline;
    juce::String lastState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};

} // namespace encore
