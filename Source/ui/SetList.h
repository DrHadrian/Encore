// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"

namespace encore
{

/** The running order in the edit view's sidebar: called "Set list" (songs) or
    "Cue list" (cues) depending on the kind of show, with its sections as
    headings. Click to go to an item; Space/arrow keys step through (the main
    window handles those). Double-click to rename; right-click for options.

    The same list opens full size (all columns, cut items, fast typing) with
    the expand button: that's CueListEditor, on the same data. */
class SetList final : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit SetList (ShowSession& session);

    /** Re-reads the running order and the playing position from the session. */
    void refresh();

    /** The expand button: open the full list. */
    std::function<void()> onExpand;

    void resized() override;

private:
    struct Row
    {
        juce::String id;
        int cueIndex = -1;       // -1: a section heading
        juce::String heading;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;

    int cueIndexOfRow (int row) const;
    void rename (int cueIndex);
    void showCueMenu (int cueIndex);
    void updateButtons();

    ShowSession& session;
    std::vector<Row> rows;

    juce::Label header;
    juce::TextButton expandButton { "Full list" };
    juce::ListBox list { "Running order", this };
    juce::TextButton addButton { "+ Add" }, removeButton { "Remove" }, upButton { "Up" }, downButton { "Down" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetList)
};

} // namespace encore
