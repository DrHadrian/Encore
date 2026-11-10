// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"

namespace encore
{

/** The running order: songs or cues in playing order, each pointing at a
    patch. Click a cue to go to it; Space/arrow keys step through (handled by
    the main window). Double-click to rename; right-click to choose its patch,
    its same-patch behaviour, or to remove it.

    This is the simple, flat version; the theatre cue list (acts, scenes, bar
    marks, decimal numbers, cut cues) comes in milestone 8 on the same data. */
class SetList final : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit SetList (ShowSession& session);

    /** Re-reads the cues and the playing position from the session. */
    void refresh();

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void rename (int row);
    void showCueMenu (int row);

    ShowSession& session;
    juce::Label header { {}, "Set list" };
    juce::ListBox list { "Set list", this };
    juce::TextButton addButton { "+ Add" }, removeButton { "Remove" }, upButton { "Up" }, downButton { "Down" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetList)
};

} // namespace encore
