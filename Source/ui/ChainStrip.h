// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/core/Chain.h"
#include "session/ShowSession.h"

namespace encore
{

/** The patch's signal chain as a row of tiles, left to right (Helix/MainStage
    style). Click a tile to edit it below; right-click to move, insert or
    remove; the "+" tile adds a block at the end. Message thread only. */
class ChainStrip final : public juce::Component
{
public:
    explicit ChainStrip (ShowSession& session);

    /** The live chain to show (null while none). Keeps the selection if possible. */
    void setChain (Chain* chain);
    Chain* getChain() const noexcept { return chain; }

    int getSelectedIndex() const noexcept { return selected; }
    std::function<void (int)> onSelectionChanged;

    /** Call from a UI timer: tile states (on/off, loaded file) can change anywhere. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void select (int index);
    void showBlockMenu (int index);
    void showAddMenu (int insertAt);
    juce::PopupMenu blockTypeMenu (int insertAt);

    ShowSession& session;
    Chain* chain = nullptr;
    int selected = 0;
    std::vector<juce::Rectangle<int>> tiles;
    juce::Rectangle<int> addTile;
    juce::String lastState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};

} // namespace encore
