// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/SetList.h"
#include "ui/CueDialogs.h"

namespace encore
{

namespace
{
    const auto playingGreen = juce::Colour (0xff2e9e5b);
    const auto amber = juce::Colour (0xfff0b429);
}

SetList::SetList (ShowSession& s) : session (s)
{
    header.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    header.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (header);

    expandButton.setTooltip ("The full list: sections, bar marks, notes, cut items, fast typing (F6)");
    expandButton.onClick = [this] { if (onExpand != nullptr) onExpand(); };
    addAndMakeVisible (expandButton);

    list.setRowHeight (38);
    list.setWantsKeyboardFocus (false);   // Space/arrows always step the show (main window)
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1b1e23));
    addAndMakeVisible (list);

    // A new item plays the current patch, right after the playing one.
    addButton.onClick = [this] { session.addCue (session.getActivePatchId(), session.getCurrentCue()); };
    removeButton.onClick = [this] { session.removeCue (cueIndexOfRow (list.getSelectedRow())); };

    for (auto [button, direction] : { std::pair { &upButton, -1 }, std::pair { &downButton, 1 } })
    {
        button->onClick = [this, direction = direction]
        {
            const auto row = list.getSelectedRow();

            if (! juce::isPositiveAndBelow (row, (int) rows.size()))
                return;

            const auto id = rows[(size_t) row].id;
            session.moveItem (id, direction);   // steps in and out of sections too
            refresh();

            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(size_t) i].id == id)
                    list.selectRow (i, false, true);
        };
    }

    for (auto* b : { &addButton, &removeButton, &upButton, &downButton })
        addAndMakeVisible (b);

    refresh();
}

void SetList::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto words = session.getWords();

    header.setText (words.list, juce::dontSendNotification);
    list.setTooltip ("Click to go to a " + words.item + ". Space or arrow keys step through.\n"
                     "Double-click to rename, right-click for options.");
    addButton.setTooltip ("New " + words.item + " after the playing one");

    // Sections as headings, playable items; cut items only show in the full list.
    rows.clear();

    for (auto& r : session.getRunningOrderRows())
    {
        if (r.item->kind == RunItem::Kind::section)
            rows.push_back ({ r.item->id, -1, r.item->name });
        else if (r.cueIndex >= 0)
            rows.push_back ({ r.item->id, r.cueIndex, {} });
    }

    list.updateContent();

    const auto current = session.getCurrentCue();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (rows[(size_t) i].cueIndex == current && current >= 0)
        {
            list.selectRow (i, false, true);
            list.scrollToEnsureRowIsOnscreen (i);
        }
    }

    updateButtons();
    list.repaint();
}

int SetList::cueIndexOfRow (int row) const
{
    return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row].cueIndex : -1;
}

void SetList::updateButtons()
{
    const auto row = list.getSelectedRow();
    const auto hasSelection = juce::isPositiveAndBelow (row, (int) rows.size());
    removeButton.setEnabled (hasSelection && rows[(size_t) row].cueIndex >= 0);
    upButton.setEnabled (hasSelection);
    downButton.setEnabled (hasSelection);
}

int SetList::getNumRows()
{
    return (int) rows.size();
}

void SetList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    if (rows[(size_t) row].cueIndex < 0)
    {
        // A section heading (act, scene, set...).
        g.setColour (amber.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (rows[(size_t) row].heading.toUpperCase(), juce::Rectangle<int> (10, 0, width - 14, height - 4),
                    juce::Justification::bottomLeft, true);
        return;
    }

    const auto cues = session.getCues();
    const auto index = rows[(size_t) row].cueIndex;

    if (! juce::isPositiveAndBelow (index, (int) cues.size()))
        return;

    const auto& cue = *cues[(size_t) index];
    const auto playing = index == session.getCurrentCue();
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2);

    if (selected || playing)
    {
        g.setColour (juce::Colour (0xff2a3140));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
    }

    if (playing)
    {
        g.setColour (playingGreen);
        g.fillRoundedRectangle (area.removeFromLeft (6).toFloat().reduced (0.0f, 4.0f), 3.0f);
    }
    else
    {
        area.removeFromLeft (6);
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (cue.number, area.removeFromLeft (38).withTrimmedLeft (6), juce::Justification::centredLeft);

    // Patch name on the right.
    const auto* patch = cue.uniquePatch ? &*cue.uniquePatch : session.getShow().findPatch (cue.patchId);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (patch != nullptr ? juce::Colours::white.withAlpha (0.5f) : amber);
    const auto patchText = patch != nullptr ? patch->name : juce::String ("(patch missing)");
    const auto midiText = juce::String (cue.programChange >= 0 ? "PC " + juce::String (cue.programChange) + "  " : "")
                        + (cue.midiOut.empty() ? "" : "OUT  ");
    g.drawText (midiText + patchText, area.removeFromRight (juce::jmin (110, width / 3)), juce::Justification::centredRight, true);

    g.setColour (juce::Colours::white.withAlpha (playing ? 1.0f : 0.85f));
    g.setFont (juce::FontOptions (16.0f, playing ? juce::Font::bold : juce::Font::plain));
    g.drawText (cue.name.isNotEmpty() ? cue.name : patchText, area.reduced (4, 0), juce::Justification::centredLeft, true);
}

void SetList::selectedRowsChanged (int)
{
    if (! updating)
        updateButtons();
}

void SetList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto index = cueIndexOfRow (row);

    if (index < 0)
        return;   // a heading

    if (e.mods.isPopupMenu())
        showCueMenu (index);
    else
        session.goToCue (index);
}

void SetList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (const auto index = cueIndexOfRow (row); index >= 0)
        rename (index);
    else if (onExpand != nullptr)
        onExpand();   // sections are edited in the full list
}

void SetList::rename (int index)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (index, (int) cues.size()))
        return;

    auto* window = new juce::AlertWindow ("Rename " + session.getWords().item, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", cues[(size_t) index]->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<SetList> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window, index] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->session.renameCue (index, window->getTextEditorContents ("name"));
    }), true);
}

void SetList::showCueMenu (int index)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (index, (int) cues.size()))
        return;

    const auto& cue = *cues[(size_t) index];
    const auto words = session.getWords();
    juce::Component::SafePointer<SetList> safeThis (this);

    juce::PopupMenu patches;

    for (auto& p : session.getShow().patches)
        patches.addItem (p.name, true, ! cue.uniquePatch && p.id == cue.patchId, [safeThis, index, id = p.id]
        {
            if (safeThis != nullptr)
                safeThis->session.setCuePatch (index, id);
        });

    const auto reset = cue.samePatch == SamePatchBehaviour::reset;
    juce::PopupMenu samePatch;
    samePatch.addItem ("Keep playing as it is", true, ! reset, [safeThis, index]
    {
        if (safeThis != nullptr) safeThis->session.setCueSamePatch (index, SamePatchBehaviour::keep);
    });
    samePatch.addItem ("Reset to its saved settings", true, reset, [safeThis, index]
    {
        if (safeThis != nullptr) safeThis->session.setCueSamePatch (index, SamePatchBehaviour::reset);
    });

    juce::PopupMenu menu;
    menu.addItem ("Go to this " + words.item, [safeThis, index] { if (safeThis != nullptr) safeThis->session.goToCue (index); });
    menu.addItem ("Rename...", [safeThis, index] { if (safeThis != nullptr) safeThis->rename (index); });
    menu.addSubMenu ("Patch", patches);
    menu.addSubMenu ("If the previous " + words.item + " uses the same patch", samePatch);
    menu.addSeparator();
    menu.addItem ("Recall with a program change...", [safeThis, index]
    {
        if (safeThis != nullptr) cuedialogs::editProgramChange (*safeThis, safeThis->session, index);
    });
    menu.addItem ("MIDI out when this " + words.item + " starts...", [safeThis, index]
    {
        if (safeThis != nullptr) cuedialogs::editMidiOut (*safeThis, safeThis->session, index);
    });
    menu.addSeparator();
    menu.addItem ("Remove", [safeThis, index] { if (safeThis != nullptr) safeThis->session.removeCue (index); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void SetList::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (30);
    expandButton.setBounds (top.removeFromRight (80).reduced (0, 3));
    header.setBounds (top);

    auto buttons = area.removeFromBottom (34);
    const auto w = buttons.getWidth() / 4;
    addButton.setBounds (buttons.removeFromLeft (w).reduced (2));
    removeButton.setBounds (buttons.removeFromLeft (w).reduced (2));
    upButton.setBounds (buttons.removeFromLeft (w).reduced (2));
    downButton.setBounds (buttons.reduced (2));

    area.removeFromBottom (6);
    list.setBounds (area);
}

} // namespace encore
