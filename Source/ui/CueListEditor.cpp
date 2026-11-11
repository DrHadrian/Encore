// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/CueListEditor.h"
#include "ui/CueDialogs.h"
#include "ui/kit/KeyCap.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    // Rows 28 + the 1 px line under them; header 24 + line; toolbar 40 + line.
    constexpr int rowHeight = size::tableRowHeight + 1, headerHeight = size::tableHeaderHeight + 1,
                  toolbarHeight = size::subToolbar + 1, indentPerLevel = size::indentPerLevel;

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
        // The editing field (DESIGN-SYSTEM.md §5.3): dark, accent border, radius 3.
        setFont (font (type::tableRow));
        setColour (juce::TextEditor::backgroundColourId, paint::c (colour::cellEditorFill));
        setColour (juce::TextEditor::textColourId, paint::c (colour::textStrong));
        setColour (juce::TextEditor::outlineColourId, paint::c (colour::accent));
        setColour (juce::TextEditor::focusedOutlineColourId, paint::c (colour::accent));
        setColour (juce::TextEditor::highlightColourId, paint::c (colour::textSelection));
        setColour (juce::CaretComponent::caretColourId, paint::c (colour::textStrong));
        setJustification (juce::Justification::centredLeft);
        setIndents (size::cellEditorPadX, 0);
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
    setOpaque (false);
    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, paint::c (colour::listBackground));
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

    title.setFont (font (type::subToolbarTitle));
    title.setColour (juce::Label::textColourId, paint::c (colour::text));
    title.setBorderSize ({});
    addAndMakeVisible (title);

    setButtonKeyCap (closeButton, "F6");
    closeButton.setTooltip ("Back to the patches and the compact list (F6)");
    closeButton.setWantsKeyboardFocus (false);
    closeButton.onClick = [this] { if (isTyping()) commitEdit(); if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    summary.setFont (font (type::subtitle));
    summary.setColour (juce::Label::textColourId, paint::c (colour::textTertiary));
    summary.setJustificationType (juce::Justification::centredRight);
    summary.setBorderSize ({});
    addAndMakeVisible (summary);

    refresh();
}

CueListEditor::~CueListEditor() = default;

bool CueListEditor::isTyping() const noexcept { return editor != nullptr && editor->isVisible(); }

void CueListEditor::refresh()
{
    const auto selected = selectedId();
    rows = session.getRunningOrderRows();
    cutNameWidths.clear();

    // The strike-through of a cut item ends with its name (measured here, not in paint).
    for (auto& row : rows)
        cutNameWidths.push_back (row.item->cut ? textWidth (font (type::tableRow), row.item->name) : 0.0f);

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
    itemTitle = words.item.toUpperCase();
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

    // DESIGN-SYSTEM.md §5.5: 28 px rows and a line, sections on the header
    // gradient, every other row a shade lighter, the playing row with a
    // green bar and bold, the selected row accent 35 %, cut items faded.
    const auto& r = rows[(size_t) row];
    const auto& item = *r.item;
    const auto indent = r.depth * indentPerLevel;
    const auto playing = r.cueIndex >= 0 && r.cueIndex == session.getCurrentCue();
    const auto bounds = juce::Rectangle<int> (0, 0, width, height);
    const auto line = bounds.withHeight (height - 1).toFloat();

    if (selected)
    {
        g.setColour (paint::c (colour::tableSelection));
        g.fillRect (line);
    }
    else if (item.kind == RunItem::Kind::section)
    {
        g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::paneHeaderTop), 0.0f, paint::c (colour::paneHeaderBot), line.getBottom()));
        g.fillRect (line);
    }
    else if (row % 2 == 1)
    {
        g.setColour (paint::c (colour::rowAlternate));
        g.fillRect (line);
    }

    if (playing)
    {
        g.setColour (paint::c (colour::healthy));
        g.fillRect (line.withWidth ((float) size::playingBar));
    }

    g.setColour (paint::c (colour::rowLine));
    paint::hline (g, 0.0f, (float) height - 1.0f, (float) width);

    const auto cell = [&] (Column c) { return columnBounds (c, width).withHeight (height - 1).reduced (size::cellPadX, 0).toFloat(); };

    if (item.kind == RunItem::Kind::section)
    {
        auto area = line.withTrimmedLeft ((float) (indent + size::sectionPadX));
        paint::text (g, sectionKindLabel (item.sectionKind), area.removeFromLeft ((float) size::sectionKindW), type::tableKind, colour::amberSection);
        paint::text (g, item.name.isEmpty() ? juce::String ("(unnamed)") : item.name, area, type::tableSection, colour::textStrong);
        return;
    }

    const auto alpha = item.cut ? state::cutAlpha : 1.0f;
    const auto rowStyle = type::Style { type::tableRow.px, playing ? type::Weight::bold : type::Weight::regular };
    const auto midiStyle = type::Style { type::tableMidi.px, playing ? type::Weight::bold : type::Weight::regular };
    const auto tint = [alpha] (juce::uint32 c, float a) { return paint::c (c).withMultipliedAlpha (a * alpha); };

    g.setFont (font (rowStyle));
    g.setColour (tint (colour::text, 0.7f));
    g.drawText (item.cut ? juce::String ("cut") : item.number, cell (Column::number), juce::Justification::centredRight);

    const auto nameArea = cell (Column::name).withTrimmedLeft ((float) indent);
    g.setColour (tint (colour::text, 1.0f));
    g.drawText (item.name, nameArea, juce::Justification::centredLeft, true);

    if (item.cut && (size_t) row < cutNameWidths.size())
        paint::hline (g, nameArea.getX(), nameArea.getCentreY(), juce::jmin (nameArea.getWidth(), cutNameWidths[(size_t) row]));

    g.setColour (tint (colour::amber, 1.0f));
    g.drawText (item.barMark, cell (Column::bar), juce::Justification::centredLeft, true);

    g.setColour (tint (colour::text, 0.75f));
    g.drawText (item.note, cell (Column::note), juce::Justification::centredLeft, true);

    juce::String patch;

    if (item.uniquePatch)
        patch = item.uniquePatch->name + " (own)";
    else if (const auto* p = session.getShow().findPatch (item.patchId))
        patch = p->name;
    else
        patch = "(missing)";

    g.setColour (tint (colour::text, 0.6f));
    g.drawText (patch, cell (Column::patch), juce::Justification::centredLeft, true);

    juce::StringArray midi;

    if (item.programChange >= 0)
        midi.add ("PC " + juce::String (item.programChange));

    if (! item.midiOut.empty())
        midi.add ("OUT");

    g.setFont (font (midiStyle));
    g.drawText (midi.joinIntoString ("  "), cell (Column::midi), juce::Justification::centredLeft, true);
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

    editor->setBounds (cell.withTrimmedRight (size::cellEditorInsetR).withSizeKeepingCentre (cell.getWidth() - size::cellEditorInsetR, size::cellEditorH));
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
    // One pane: the sub-toolbar, the column titles, the table.
    const auto bounds = getLocalBounds().toFloat();
    paint::pane (g, bounds);

    const auto inner = getLocalBounds().reduced (1);
    auto bar = inner.withHeight (size::subToolbar).toFloat();
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::subToolbarTop), bar.getY(), paint::c (colour::subToolbarBot), bar.getBottom()));
    g.fillRect (bar);
    g.setColour (paint::c (colour::paneHeaderLine));
    paint::hline (g, bar.getX(), bar.getBottom(), bar.getWidth());

    auto header = inner.withTrimmedTop (toolbarHeight).withHeight (size::tableHeaderHeight).toFloat();
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::paneHeaderTop), header.getY(), paint::c (colour::paneHeaderBot), header.getBottom()));
    g.fillRect (header);
    g.setColour (paint::c (colour::paneHeaderLine));
    paint::hline (g, header.getX(), header.getBottom(), header.getWidth());

    const std::pair<Column, const char*> titles[] = { { Column::number, "#" }, { Column::name, nullptr }, { Column::bar, "BAR" },
                                                      { Column::note, "NOTE" }, { Column::patch, "PATCH" }, { Column::midi, "MIDI" } };

    for (auto& [column, text] : titles)
        paint::text (g, text != nullptr ? juce::String (text) : itemTitle,
                     columnBounds (column, list.getVisibleRowWidth()).toFloat().withY (header.getY()).withHeight (header.getHeight())
                         .translated ((float) list.getX(), 0.0f).reduced ((float) size::cellPadX, 0.0f),
                     type::paneHeader, colour::textMuted,
                     column == Column::number ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void CueListEditor::resized()
{
    // Toolbar: title (98), + Cue, + Section, Cut, Delete, Up, Down, Go to
    // (70, 90 for + Section; 6 apart), the summary, Back to patches (130).
    auto area = getLocalBounds().reduced (1);
    auto toolbar = area.removeFromTop (toolbarHeight).withTrimmedBottom (1).reduced (size::subToolbarPadX, 0);
    const auto buttonY = toolbar.getY() + (toolbar.getHeight() - size::buttonHeight) / 2;
    const auto take = [&] (juce::Component& c, int w) { c.setBounds (toolbar.removeFromLeft (w).withY (buttonY).withHeight (size::buttonHeight + size::buttonDrop)); toolbar.removeFromLeft (size::subToolbarGap); };

    title.setBounds (toolbar.removeFromLeft (size::subToolbarTitleW));
    toolbar.removeFromLeft (size::subToolbarGap);

    const auto backW = juce::jmax (size::backButtonW, closeButton.getBestWidthForHeight (size::buttonHeight));
    closeButton.setBounds (toolbar.removeFromRight (backW).withY (buttonY).withHeight (size::buttonHeight + size::buttonDrop));
    toolbar.removeFromRight (size::subToolbarGap);

    for (auto* b : { &addCueButton, &addSectionButton, &cutButton, &deleteButton, &upButton, &downButton, &goButton })
        take (*b, b == &addSectionButton ? size::subToolbarWideButton : size::subToolbarButton);

    summary.setBounds (toolbar);
    area.removeFromTop (headerHeight);
    list.setBounds (area);

    if (isTyping())
        commitEdit();   // the cell moved
}

} // namespace encore
