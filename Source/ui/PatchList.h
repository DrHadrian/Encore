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

/** The show's patch library: click to select (and hear) a patch, double-click
    to rename, buttons to add, duplicate and delete.
    Looks: docs/design/SCREENS.md §4.3. */
class PatchList final : public kit::Pane,
                        private kit::RowList::Model
{
public:
    explicit PatchList (ShowSession& session);

    /** Re-reads names and the selection from the session. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    int getRowHeight (int) override;
    void paintRow (juce::Graphics&, int row, juce::Rectangle<int>) override;
    void rowClicked (int row, const juce::MouseEvent&) override;
    void rowDoubleClicked (int row, const juce::MouseEvent&) override;
    void showPatchMenu (int row);

    void rename (int row);
    void confirmDelete();
    juce::Rectangle<int> getFooterBounds() const;

    ShowSession& session;
    kit::RowList list { *this };
    kit::IconButton addButton { theme::Icon::plus, "New patch" }, duplicateButton { theme::Icon::duplicate, "Duplicate patch" },
                    deleteButton { theme::Icon::trash, "Delete patch" };
    juce::String footer { "A patch can be used by several cues. Editing it changes every cue that uses it." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchList)
};

} // namespace encore
