// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"

namespace encore
{

/** The show's patch library: click to select (and hear) a patch, double-click
    to rename, buttons to add, duplicate and delete. */
class PatchList final : public juce::Component,
                        private juce::ListBoxModel
{
public:
    explicit PatchList (ShowSession& session);

    /** Re-reads names and the selection from the session. */
    void refresh();

    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void showPatchMenu (int row);
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void rename (int row);
    void confirmDelete();

    ShowSession& session;
    juce::Label header { {}, "Patches" };
    juce::ListBox list { "Patches", this };
    juce::TextButton addButton { "+ New" }, duplicateButton { "Duplicate" }, deleteButton { "Delete" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchList)
};

} // namespace encore
