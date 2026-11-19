// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/CueListEditor.h"
#include "ui/CueDialogs.h"

namespace encore
{

namespace
{
    const auto background   = juce::Colour (0xff16181c);
    const auto headerColour = juce::Colour (0xff202329);
    const auto accent       = juce::Colour (0xff4c9aff);
    const auto playingGreen = juce::Colour (0xff2e9e5b);
    const auto amber        = juce::Colour (0xfff0b429);

    constexpr int rowHeight = 28, headerHeight = 24, toolbarHeight = 34, indentPerLevel = 18;

    juce::String sectionKindLabel (const juce::String& kind)
    {
        return kind.isEmpty() ? juce::String ("SECTION") : kind.toUpperCase();
    }
}

//==============================================================================
/** A text field over the cell being typed in: Enter, Tab and Esc go back to the list. */
class CueListEditor::CellEditor final : public juce::TextEditor
{
public:
    explicit CellEditor (CueListEditor& o) : owner (o)
    {
        setFont (juce::FontOptions (15.0f));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e1013));
        setColour (juce::TextEditor::outlineColourId, accent);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setIndents (6, 4);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();

        if (code == juce::KeyPress::returnKey || code == juce::KeyPress::tabKey || code == juce::KeyPress::escapeKey)
        {
            owner.editorKey (key);
            return true;
        }

        return juce::TextEditor::keyPressed (key);
    }

    void focusLost (FocusChangeType) override
    {
        // Clicking elsewhere keeps what was typed.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<CueListEditor> (&owner)]
        {
            if (safe != nullptr && safe->isTyping() && ! safe->editor->hasKeyboardFocus (false))
                safe->commitEdit();
        });
    }

private:
    CueListEditor& owner;
};

//==============================================================================
CueListEditor::CueListEditor (ShowSession& s) : session (s)
{
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, background);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setWantsKeyboardFocus (false);
    list.getViewport()->setWantsKeyboardFocus (false);
    addAndMakeVisible (list);

    addCueButton.setTooltip ("New cue after the selected one (Insert or Ctrl+Enter)");
    addCueButton.onClick = [this] { insertCueAfterSelection (true); };
    addSectionButton.setTooltip ("New act, scene or song after the selected item (Ctrl+Shift+Enter)");
    addSectionButton.onClick = [this] { insertSectionAfterSelection(); };
    cutButton.setTooltip ("Cut: the cue stays in the list but the show skips it (and it loses its number)");
    cutButton.onClick = [this] { toggleCutSelection(); };
    deleteButton.setTooltip ("Delete the selected cue or section (a section's cues stay) (Delete)");
    deleteButton.onClick = [this] { deleteSelection(); };
    upButton.setTooltip ("Move up (Ctrl+Up)");
    upButton.onClick = [this] { moveSelection (-1); };
    downButton.setTooltip ("Move down (Ctrl+Down)");
    downButton.onClick = [this] { moveSelection (+1); };
    goButton.setTooltip ("Play the selected cue now (G). Typing a number and Enter anywhere also goes to that cue.");
    goButton.onClick = [this] { goToSelection(); };

    for (auto* b : { &addCueButton, &addSectionButton, &cutButton, &deleteButton, &upButton, &downButton, &goButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    title.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    closeButton.setTooltip ("Back to the patches and the compact list (F6)");
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (isTyping()) commitEdit(); if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    summary.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    summary.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (summary);

    refresh();
}

CueListEditor::~CueListEditor() = default;

bool CueListEditor::isTyping() const noexcept { return editor != nullptr && editor->isVisible(); }

void CueListEditor::refresh()
{
    const auto selected = selectedId();
    rows = session.getRunningOrderRows();
    list.updateContent();

    if (selected.isNotEmpty())
        selectId (selected);

    int cues = 0, cut = 0, sections = 0;

    for (auto& row : rows)
    {
        sections += row.item->kind == RunItem::Kind::section ? 1 : 0;
        cues += row.cueIndex >= 0 ? 1 : 0;
        cut += row.item->kind == RunItem::Kind::cue && row.item->cut ? 1 : 0;
    }

    const auto words = session.getWords();
    title.setText (words.list, juce::dontSendNotification);
    addCueButton.setButtonText ("+ " + words.Item());
    addCueButton.setTooltip ("New " + words.item + " after the selected one (Insert or Ctrl+Enter)");

    summary.setText (juce::String (cues) + " " + words.items + (cut > 0 ? ", " + juce::String (cut) + " cut" : juce::String())
                         + (sections > 0 ? ", " + juce::String (sections) + " sections" : juce::String()),
                     juce::dontSendNotification);

    const auto* item = juce::isPositiveAndBelow (list.getSelectedRow(), (int) rows.size()) ? rows[(size_t) list.getSelectedRow()].item : nullptr;
    cutButton.setButtonText (item != nullptr && item->cut ? "Restore" : "Cut");
    cutButton.setEnabled (item != nullptr && item->kind == RunItem::Kind::cue);
    list.repaint();
}

//==============================================================================
juce::Rectangle<int> CueListEditor::columnBounds (Column column, int width) const
{
    // number | name | bar | note | patch | midi
    const auto fixed = 64 + 150 + 190 + 90;
    const auto flexible = juce::jmax (200, width - fixed);
    const int widths[] = { 64, flexible * 45 / 100, 150, flexible - flexible * 45 / 100, 190, 90 };

    int x = 0;

    for (int i = 0; i < (int) column; ++i)
        x += widths[i];

    return { x, 0, widths[(int) column], rowHeight };
}

CueListEditor::Column CueListEditor::columnAt (int x) const
{
    for (auto c : { Column::number, Column::name, Column::bar, Column::note, Column::patch, Column::midi })
        if (columnBounds (c, list.getVisibleRowWidth()).getRight() > x)
            return c;

    return Column::midi;
}

int CueListEditor::getNumRows() { return (int) rows.size(); }

void CueListEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& r = rows[(size_t) row];
    const auto& item = *r.item;
    const auto indent = r.depth * indentPerLevel;
    const auto playing = r.cueIndex >= 0 && r.cueIndex == session.getCurrentCue();

    if (selected)
        g.fillAll (accent.withAlpha (0.35f));
    else if (item.kind == RunItem::Kind::section)
        g.fillAll (headerColour);
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.025f));

    if (playing)
    {
        g.setColour (playingGreen);
        g.fillRect (0, 0, 4, height);
    }

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);

    const auto cell = [&] (Column c) { return columnBounds (c, width).reduced (6, 0); };

    if (item.kind == RunItem::Kind::section)
    {
        auto area = juce::Rectangle<int> (indent + 10, 0, width - indent - 10, height);
        g.setColour (amber.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (sectionKindLabel (item.sectionKind), area.removeFromLeft (70), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (item.name.isEmpty() ? juce::String ("(unnamed)") : item.name, area, juce::Justification::centredLeft, true);
        return;
    }

    const auto alpha = item.cut ? 0.35f : 1.0f;
    g.setFont (juce::FontOptions (15.0f, playing ? juce::Font::bold : juce::Font::plain));

    g.setColour (juce::Colours::white.withAlpha (0.7f * alpha));
    g.drawText (item.cut ? juce::String ("cut") : item.number, cell (Column::number), juce::Justification::centredRight);

    auto nameArea = cell (Column::name).withTrimmedLeft (indent);
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.drawText (item.name, nameArea, juce::Justification::centredLeft, true);

    if (item.cut)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        const auto textWidth = juce::jmin ((float) nameArea.getWidth(),
                                           juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), item.name));
        g.drawHorizontalLine (height / 2, (float) nameArea.getX(), (float) nameArea.getX() + textWidth);
    }

    g.setColour (amber.withAlpha (alpha));
    g.drawText (item.barMark, cell (Column::bar), juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::white.withAlpha (0.75f * alpha));
    g.drawText (item.note, cell (Column::note), juce::Justification::centredLeft, true);

    juce::String patch;

    if (item.uniquePatch)
        patch = item.uniquePatch->name + " (own)";
    else if (const auto* p = session.getShow().findPatch (item.patchId))
        patch = p->name;
    else
        patch = "(missing)";

    g.setColour (juce::Colours::white.withAlpha (0.6f * alpha));
    g.drawText (patch, cell (Column::patch), juce::Justification::centredLeft, true);

    juce::StringArray midi;

    if (item.programChange >= 0)
        midi.add ("PC " + juce::String (item.programChange));

    if (! item.midiOut.empty())
        midi.add ("OUT");

    g.drawText (midi.joinIntoString (" "), cell (Column::midi), juce::Justification::centredLeft, true);
}

void CueListEditor::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (isTyping())
        commitEdit();

    if (e.mods.isPopupMenu())
    {
        list.selectRow (row);
        showRowMenu (row);
        return;
    }

    if (columnAt (e.x) == Column::patch && juce::isPositiveAndBelow (row, (int) rows.size())
        && rows[(size_t) row].item->kind == RunItem::Kind::cue && list.isRowSelected (row))
        showPatchMenu (row);

    refresh();
}

void CueListEditor::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    const auto column = columnAt (e.x);

    if (column == Column::number)
    {
        goToSelection();
        return;
    }

    if (column == Column::patch)
    {
        showPatchMenu (row);
        return;
    }

    if (column == Column::midi)
    {
        showRowMenu (row);
        return;
    }

    startEditing (row, column, false);
}

//==============================================================================
void CueListEditor::startEditing (int row, Column column, bool chaining)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& item = *rows[(size_t) row].item;

    // Sections only have a name.
    if (item.kind == RunItem::Kind::section)
        column = Column::name;

    if (column != Column::name && column != Column::bar && column != Column::note)
        column = Column::name;

    list.selectRow (row);
    list.scrollToEnsureRowIsOnscreen (row);

    editingId = item.id;
    editingColumn = column;
    chainingEntry = chaining;

    if (editor == nullptr)
    {
        editor = std::make_unique<CellEditor> (*this);
        addChildComponent (*editor);
    }

    const auto text = column == Column::name ? item.name : column == Column::bar ? item.barMark : item.note;
    editor->setText (text, false);
    editor->selectAll();

    auto rowArea = list.getRowPosition (row, true).translated (list.getX(), list.getY());
    auto cell = columnBounds (column, list.getVisibleRowWidth()).withY (rowArea.getY()).translated (list.getX(), 0);

    if (column == Column::name)
        cell = cell.withTrimmedLeft (rows[(size_t) row].depth * indentPerLevel
                                     + (item.kind == RunItem::Kind::section ? 80 : 0));

    editor->setBounds (cell.expanded (0, 1));
    editor->setVisible (true);
    editor->toFront (false);

    if (editor->isShowing())
        editor->grabKeyboardFocus();
}

void CueListEditor::commitEdit()
{
    if (! isTyping())
        return;

    const auto text = editor->getText();
    const auto id = editingId;
    editor->setVisible (false);

    if (editingColumn == Column::name)        session.setItemName (id, text);
    else if (editingColumn == Column::bar)    session.setItemBarMark (id, text);
    else if (editingColumn == Column::note)   session.setItemNote (id, text);

    refresh();
}

void CueListEditor::cancelEdit()
{
    if (editor != nullptr)
        editor->setVisible (false);

    chainingEntry = false;
}

void CueListEditor::editorKey (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto id = editingId;
    const auto column = editingColumn;
    const auto chaining = chainingEntry;

    if (code == juce::KeyPress::escapeKey)
    {
        cancelEdit();
        return;
    }

    const auto typed = editor->getText().trim();
    commitEdit();

    auto rowOf = [this] (const juce::String& itemId)
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].item->id == itemId)
                return i;

        return -1;
    };

    const auto row = rowOf (id);

    if (code == juce::KeyPress::tabKey)
    {
        // Name -> bar -> note -> next row's name (Shift: backwards).
        static const Column order[] = { Column::name, Column::bar, Column::note };
        int index = column == Column::name ? 0 : column == Column::bar ? 1 : 2;
        int target = row;
        index += key.getModifiers().isShiftDown() ? -1 : 1;

        if (index > 2)       { index = 0; ++target; }
        else if (index < 0)  { index = 2; --target; }

        if (juce::isPositiveAndBelow (target, (int) rows.size()))
            startEditing (target, order[index], chaining);

        return;
    }

    // Enter: while typing a show in, a name followed by Enter makes the next
    // cue; Enter on an emptied name ends it (and drops that empty cue).
    if (chaining && column == Column::name && typed.isEmpty())
    {
        session.deleteItem (id);
        refresh();
        return;
    }

    if (chaining && column == Column::name)
    {
        selectId (id);
        insertCueAfterSelection (true);
        return;
    }

    if (row >= 0 && row + 1 < (int) rows.size())
        list.selectRow (row + 1);
}

//==============================================================================
juce::String CueListEditor::selectedId() const
{
    const auto row = list.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row].item->id : juce::String();
}

void CueListEditor::selectId (const juce::String& id)
{
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (rows[(size_t) i].item->id == id)
        {
            list.selectRow (i);
            return;
        }
    }
}

void CueListEditor::insertCueAfterSelection (bool editName)
{
    auto after = selectedId();

    if (after.isEmpty() && ! rows.empty())
        after = rows.back().item->id;

    const auto id = session.insertCue (after);
    refresh();
    selectId (id);

    if (editName)
        startEditing (list.getSelectedRow(), Column::name, true);
}

void CueListEditor::insertSectionAfterSelection()
{
    const auto words = session.getWords();
    const auto id = session.insertSection (selectedId(), words.newSection, words.sectionKind);
    refresh();
    selectId (id);
    startEditing (list.getSelectedRow(), Column::name, false);
}

void CueListEditor::deleteSelection()
{
    const auto row = list.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& item = *rows[(size_t) row].item;
    const auto id = item.id;
    const auto what = item.kind == RunItem::Kind::section
                          ? "the section \"" + item.name + "\" (its cues stay)"
                          : "cue " + (item.number.isNotEmpty() ? item.number + " " : juce::String()) + "\"" + item.name + "\"";

    juce::Component::SafePointer<CueListEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Delete")
                                      .withMessage ("Delete " + what + "?")
                                      .withButton ("Delete", 1)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [safe, id, row] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        safe->session.deleteItem (id);
        safe->refresh();
        safe->list.selectRow (juce::jmin (row, (int) safe->rows.size() - 1));
    });
}

void CueListEditor::moveSelection (int direction)
{
    const auto id = selectedId();

    if (id.isEmpty())
        return;

    session.moveItem (id, direction);
    refresh();
    selectId (id);
}

void CueListEditor::toggleCutSelection()
{
    const auto row = list.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].item->kind != RunItem::Kind::cue)
        return;

    const auto id = rows[(size_t) row].item->id;
    session.setCut (id, ! rows[(size_t) row].item->cut);
    refresh();
    selectId (id);
}

void CueListEditor::goToSelection()
{
    const auto row = list.getSelectedRow();

    if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].cueIndex >= 0)
        session.goToCue (rows[(size_t) row].cueIndex);
}

bool CueListEditor::handleKey (const juce::KeyPress& key)
{
    if (isTyping())
        return false;

    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const auto row = list.getSelectedRow();

    if ((code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) && mods.isCommandDown())
    {
        moveSelection (code == juce::KeyPress::upKey ? -1 : 1);
        return true;
    }

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const auto target = juce::jlimit (0, juce::jmax (0, (int) rows.size() - 1),
                                          (row < 0 ? 0 : row + (code == juce::KeyPress::upKey ? -1 : 1)));
        list.selectRow (target);
        refresh();
        return true;
    }

    if (code == juce::KeyPress::returnKey && mods.isCommandDown() && mods.isShiftDown())
    {
        insertSectionAfterSelection();
        return true;
    }

    if (code == juce::KeyPress::insertKey || (code == juce::KeyPress::returnKey && mods.isCommandDown()))
    {
        insertCueAfterSelection (true);
        return true;
    }

    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::F2Key)
    {
        if (rows.empty())
            insertCueAfterSelection (true);
        else
            startEditing (juce::jmax (0, row), Column::name, false);

        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (key == juce::KeyPress ('g') || key == juce::KeyPress ('G'))
    {
        goToSelection();
        return true;
    }

    return false;
}

//==============================================================================
void CueListEditor::showPatchMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].item->kind != RunItem::Kind::cue)
        return;

    const auto id = rows[(size_t) row].item->id;
    const auto current = rows[(size_t) row].item->patchId;
    juce::Component::SafePointer<CueListEditor> safe (this);
    juce::PopupMenu menu;

    for (auto& p : session.getShow().patches)
        menu.addItem (p.name, true, p.id == current, [safe, id, patchId = p.id]
        {
            if (safe != nullptr) { safe->session.setItemPatch (id, patchId); safe->refresh(); }
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition());
}

void CueListEditor::showRowMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& r = rows[(size_t) row];
    const auto id = r.item->id;
    juce::Component::SafePointer<CueListEditor> safe (this);
    juce::PopupMenu menu;

    if (r.item->kind == RunItem::Kind::section)
    {
        juce::PopupMenu kinds;

        for (auto kind : { "act", "scene", "set", "song", "number", "section" })
            kinds.addItem (juce::String (kind).substring (0, 1).toUpperCase() + juce::String (kind).substring (1),
                           true, r.item->sectionKind == kind,
                           [safe, id, kind] { if (safe != nullptr) { safe->session.setSectionKind (id, kind); safe->refresh(); } });

        menu.addItem ("Rename", [safe, row] { if (safe != nullptr) safe->startEditing (row, Column::name, false); });
        menu.addSubMenu ("Kind", kinds);
        menu.addItem ("New " + session.getWords().item + " at the start of this section", [safe] { if (safe != nullptr) safe->insertCueAfterSelection (true); });
    }
    else
    {
        const auto cueIndex = r.cueIndex;
        menu.addItem ("Go to this " + session.getWords().item + " (G)", cueIndex >= 0, false, [safe] { if (safe != nullptr) safe->goToSelection(); });
        menu.addItem ("Rename (Enter)", [safe, row] { if (safe != nullptr) safe->startEditing (row, Column::name, false); });
        menu.addItem ("Bar mark", [safe, row] { if (safe != nullptr) safe->startEditing (row, Column::bar, false); });
        menu.addItem ("Note", [safe, row] { if (safe != nullptr) safe->startEditing (row, Column::note, false); });
        menu.addItem ("Patch...", [safe, row] { if (safe != nullptr) safe->showPatchMenu (row); });
        menu.addSeparator();
        menu.addItem ("Recall with a program change...", cueIndex >= 0, false,
                      [safe, cueIndex] { if (safe != nullptr) cuedialogs::editProgramChange (*safe, safe->session, cueIndex); });
        menu.addItem ("MIDI out when this " + session.getWords().item + " starts...", cueIndex >= 0, false,
                      [safe, cueIndex] { if (safe != nullptr) cuedialogs::editMidiOut (*safe, safe->session, cueIndex); });
        menu.addSeparator();
        menu.addItem (r.item->cut ? "Restore (uncut)" : "Cut (skip in the show)", [safe] { if (safe != nullptr) safe->toggleCutSelection(); });
    }

    menu.addSeparator();
    menu.addItem ("Move up (Ctrl+Up)", [safe] { if (safe != nullptr) safe->moveSelection (-1); });
    menu.addItem ("Move down (Ctrl+Down)", [safe] { if (safe != nullptr) safe->moveSelection (+1); });
    menu.addItem ("Delete...", [safe] { if (safe != nullptr) safe->deleteSelection(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition());
}

//==============================================================================
void CueListEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Column titles.
    auto header = getLocalBounds().withTrimmedTop (toolbarHeight).removeFromTop (headerHeight);
    g.setColour (headerColour);
    g.fillRect (header);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

    const std::pair<Column, const char*> titles[] = { { Column::number, "#" }, { Column::name, nullptr }, { Column::bar, "BAR" },
                                                      { Column::note, "NOTE" }, { Column::patch, "PATCH" }, { Column::midi, "MIDI" } };

    const auto itemTitle = session.getWords().item.toUpperCase();

    for (auto& [column, text] : titles)
        g.drawText (text != nullptr ? juce::String (text) : itemTitle, columnBounds (column, list.getVisibleRowWidth()).withY (header.getY()).withHeight (headerHeight).reduced (6, 0),
                    column == Column::number ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void CueListEditor::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (0, 4);
    title.setBounds (toolbar.removeFromLeft (110));
    closeButton.setBounds (toolbar.removeFromRight (130));
    toolbar.removeFromRight (12);

    for (auto* b : { &addCueButton, &addSectionButton, &cutButton, &deleteButton, &upButton, &downButton, &goButton })
    {
        b->setBounds (toolbar.removeFromLeft (b == &addSectionButton ? 90 : 70));
        toolbar.removeFromLeft (6);
    }

    summary.setBounds (toolbar);
    area.removeFromTop (headerHeight);
    list.setBounds (area);

    if (isTyping())
        commitEdit();   // the cell moved
}

} // namespace encore
