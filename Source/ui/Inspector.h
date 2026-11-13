// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/core/Chain.h"
#include "session/ShowSession.h"
#include "ui/kit/InfoList.h"
#include "ui/kit/Pane.h"

namespace encore
{

/** The edit view's right column (docs/design/SCREENS.md §4.3): the selected
    block in words and numbers.
      BLOCK        type, model file (full path in the tooltip), format,
                   stomp switch (same as the chain's "Stomp switch" menu),
                   on/off parameters as tick boxes; for a plugin: which,
                   its status (with "Turn back on" after the crash guard
                   switched it off) and "Open plugin window..."
      PARAMETERS   every continuous parameter: a bar, the exact value
                   (double-click to type one) and the pedal that moves it
      ASSIGNMENTS  the patch's stomp switches and expression pedals
    It reads the block's atomics from the UI timer (refresh) and repaints
    only what changed. Message thread only. */
class Inspector final : public kit::Pane
{
public:
    explicit Inspector (ShowSession& session);
    ~Inspector() override;

    /** The block to show (null: none) and its place in the chain. */
    void setBlock (Chain* chain, int blockIndex);

    /** The IN or OUT tile (milestone 11): read-only sections instead of a
        block ("Inspector · Output", "this patch"). Empty sections: none. */
    void setInfo (const juce::String& kind, const juce::String& right, const std::vector<kit::InfoList::Section>&);
    std::function<void (int section)> onInfoButton;

    /** Call from the UI timer. */
    void refresh();

    /** Called after an edit made here (a tick box, a typed value, the stomp switch). */
    std::function<void()> onEdited;

    /** Plugin blocks: "Open plugin window...". */
    std::function<void()> onOpenPluginWindow;

    void resized() override;

private:
    class Content;

    ShowSession& session;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    kit::InfoList info;
    bool showingInfo = false;
    Chain* chain = nullptr;
    int blockIndex = -1;
    juce::String shownCpu;
    int shownLatency = 0;

    void refreshMergeRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};

} // namespace encore
