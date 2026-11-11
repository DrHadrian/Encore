// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"
#include "ui/kit/IconButton.h"
#include "ui/kit/Pane.h"
#include "ui/kit/RowList.h"

namespace encore
{

/** The running order in the edit view's sidebar: called "Set list" (songs) or
    "Cue list" (cues) depending on the kind of show, with its sections as
    headings. Click to go to an item; Space/arrow keys step through (the main
    window handles those). Double-click to rename; right-click for options.

    The same list opens full size (all columns, cut items, fast typing) with
    the Full list button: that's CueListEditor, on the same data.
    Looks: docs/design/SCREENS.md §4.3. */
class SetList final : public kit::Pane,
                      private kit::RowList::Model
{
public:
    explicit SetList (ShowSession& session);

    /** Re-reads the running order and the playing position from the session. */
    void refresh();

    /** The Full list button: open the full list. */
    std::function<void()> onExpand;

    void resized() override;

private:
    struct Row
    {
        juce::String id;
        int cueIndex = -1;       // -1: a section heading
        juce::String heading;   // in capitals
    };

    int getNumRows() override;
    int getRowHeight (int row) override;
    void paintRow (juce::Graphics&, int row, juce::Rectangle<int>) override;
    void rowClicked (int row, const juce::MouseEvent&) override;
    void rowDoubleClicked (int row, const juce::MouseEvent&) override;

    int cueIndexOfRow (int row) const;
    void selectRow (int row);
    void rename (int cueIndex);
    void showCueMenu (int cueIndex);
    void updateButtons();

    ShowSession& session;
    std::vector<Row> rows;
    int selectedRow = -1;

    juce::TextButton expandButton { "Full list" };
    kit::RowList list { *this };
    kit::IconButton addButton { theme::Icon::plus, {} }, removeButton { theme::Icon::minus, {} },
                    upButton { theme::Icon::chevronUp, {} }, downButton { theme::Icon::chevronDown, {} };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetList)
};

} // namespace encore
