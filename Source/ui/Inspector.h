// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/core/Chain.h"
#include "session/ShowSession.h"
#include "ui/kit/Pane.h"

namespace encore
{

/** The edit view's right column (docs/design/SCREENS.md §4.3): the selected
    block in words and numbers.
      BLOCK        type, model file (full path in the tooltip), format,
                   stomp switch (same as the chain's "Stomp switch" menu),
                   on/off parameters as tick boxes
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

    /** Call from the UI timer. */
    void refresh();

    /** Called after an edit made here (a tick box, a typed value, the stomp switch). */
    std::function<void()> onEdited;

    void resized() override;

private:
    class Content;

    ShowSession& session;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    Chain* chain = nullptr;
    int blockIndex = -1;
    juce::String shownCpu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};

} // namespace encore
