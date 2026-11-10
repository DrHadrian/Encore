// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"

namespace encore
{

/** The theatre cue list: the whole running order as a table (sections, cues,
    cut cues), built for typing a show in quickly.

    Columns: number (automatic), name, bar mark, note, patch, MIDI.
    Keys (when not typing in a cell):
      Up/Down select · Enter edit the name · Insert or Ctrl+Enter new cue below
      (then Enter after typing a name makes the next one) · Ctrl+Shift+Enter new
      section · Tab/Shift+Tab next/previous column while typing · Ctrl+Up/Down
      move · Delete remove · G go to the selected cue · Space still steps the show.
*/
class CueListEditor final : public juce::Component,
                            private juce::ListBoxModel
{
public:
    explicit CueListEditor (ShowSession&);
    ~CueListEditor() override;

    /** Re-reads the running order (after any session change). */
    void refresh();

    /** Keys from the main window; true if used. */
    bool handleKey (const juce::KeyPress&);

    /** True while a cell is being typed in. */
    bool isTyping() const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Column { number, name, bar, note, patch, midi };
    class CellEditor;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    juce::Rectangle<int> columnBounds (Column, int width) const;
    Column columnAt (int x) const;

    void startEditing (int row, Column column, bool chaining);
    void commitEdit();
    void cancelEdit();
    void editorKey (const juce::KeyPress&);

    juce::String selectedId() const;
    void selectId (const juce::String& id);
    void insertCueAfterSelection (bool editName);
    void insertSectionAfterSelection();
    void deleteSelection();
    void moveSelection (int direction);
    void toggleCutSelection();
    void goToSelection();
    void showRowMenu (int row);
    void showPatchMenu (int row);

    ShowSession& session;
    std::vector<ShowSession::Row> rows;

    juce::ListBox list { "Cue list", this };
    juce::TextButton addCueButton { "+ Cue" }, addSectionButton { "+ Section" }, cutButton { "Cut" },
                     deleteButton { "Delete" }, upButton { "Up" }, downButton { "Down" }, goButton { "Go to" };
    juce::Label summary;

    std::unique_ptr<CellEditor> editor;
    juce::String editingId;
    Column editingColumn = Column::name;
    bool chainingEntry = false;   // Enter after typing a name adds the next cue

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueListEditor)
};

} // namespace encore
