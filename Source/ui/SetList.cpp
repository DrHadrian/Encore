// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/SetList.h"
#include "ui/CueDialogs.h"

namespace encore
{

namespace
{
    const auto playingGreen = juce::Colour (0xff2e9e5b);
}

SetList::SetList (ShowSession& s) : session (s)
{
    header.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    header.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (header);

    list.setRowHeight (38);
    list.setWantsKeyboardFocus (false);   // Space/arrows always step the show (main window)
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1b1e23));
    list.setTooltip ("Click to go to a cue. Space or arrow keys step through.\n"
                     "Double-click to rename, right-click for options.");
    addAndMakeVisible (list);

    // A new cue plays the current patch, right after the playing cue.
    addButton.onClick = [this] { session.addCue (session.getActivePatchId(), session.getCurrentCue()); };
    removeButton.onClick = [this] { session.removeCue (list.getSelectedRow()); };
    upButton.onClick = [this]
    {
        const auto row = list.getSelectedRow();
        session.moveCue (row, -1);
        list.selectRow (row - 1, false, true);
    };
    downButton.onClick = [this]
    {
        const auto row = list.getSelectedRow();
        session.moveCue (row, 1);
        list.selectRow (row + 1, false, true);
    };

    for (auto* b : { &addButton, &removeButton, &upButton, &downButton })
        addAndMakeVisible (b);

    refresh();
}

void SetList::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    list.updateContent();

    const auto current = session.getCurrentCue();

    if (current >= 0 && current < getNumRows())
    {
        list.selectRow (current, false, true);
        list.scrollToEnsureRowIsOnscreen (current);
    }

    const auto hasSelection = list.getSelectedRow() >= 0;
    removeButton.setEnabled (hasSelection);
    upButton.setEnabled (hasSelection);
    downButton.setEnabled (hasSelection);
    list.repaint();
}

int SetList::getNumRows()
{
    return (int) session.getCues().size();
}

void SetList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (row, (int) cues.size()))
        return;

    const auto& cue = *cues[(size_t) row];
    const auto playing = row == session.getCurrentCue();
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

    // Number: the cue's own number if it has one, else its position.
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (cue.number.isNotEmpty() ? cue.number : juce::String (row + 1),
                area.removeFromLeft (38).withTrimmedLeft (6), juce::Justification::centredLeft);

    // Patch name on the right.
    const auto* patch = cue.uniquePatch ? &*cue.uniquePatch : session.getShow().findPatch (cue.patchId);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (patch != nullptr ? juce::Colours::white.withAlpha (0.5f) : juce::Colour (0xfff0b429));
    const auto patchText = patch != nullptr ? patch->name : juce::String ("(patch missing)");
    const auto midiText = juce::String (cue.programChange >= 0 ? "PC " + juce::String (cue.programChange) + "  " : "")
                        + (cue.midiOut.empty() ? "" : "OUT  ");
    g.drawText (midiText + patchText, area.removeFromRight (juce::jmin (110, width / 3)), juce::Justification::centredRight, true);

    g.setColour (juce::Colours::white.withAlpha (playing ? 1.0f : 0.85f));
    g.setFont (juce::FontOptions (16.0f, playing ? juce::Font::bold : juce::Font::plain));
    g.drawText (cue.name.isNotEmpty() ? cue.name : patchText, area.reduced (4, 0),
                juce::Justification::centredLeft, true);
}

void SetList::selectedRowsChanged (int)
{
    if (updating)
        return;

    const auto hasSelection = list.getSelectedRow() >= 0;
    removeButton.setEnabled (hasSelection);
    upButton.setEnabled (hasSelection);
    downButton.setEnabled (hasSelection);
}

void SetList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showCueMenu (row);
    else
        session.goToCue (row);
}

void SetList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    rename (row);
}

void SetList::rename (int row)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (row, (int) cues.size()))
        return;

    auto* window = new juce::AlertWindow ("Rename cue", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", cues[(size_t) row]->name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<SetList> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window, row] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->session.renameCue (row, window->getTextEditorContents ("name"));
    }), true);
}

void SetList::editProgramChange (int row) { cuedialogs::editProgramChange (*this, session, row); }
void SetList::editMidiOut (int row)       { cuedialogs::editMidiOut (*this, session, row); }

void SetList::showCueMenu (int row)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (row, (int) cues.size()))
        return;

    const auto& cue = *cues[(size_t) row];
    juce::Component::SafePointer<SetList> safeThis (this);

    juce::PopupMenu patches;

    for (auto& p : session.getShow().patches)
        patches.addItem (p.name, true, ! cue.uniquePatch && p.id == cue.patchId, [safeThis, row, id = p.id]
        {
            if (safeThis != nullptr)
                safeThis->session.setCuePatch (row, id);
        });

    const auto reset = cue.samePatch == SamePatchBehaviour::reset;
    juce::PopupMenu samePatch;
    samePatch.addItem ("Keep playing as it is", true, ! reset, [safeThis, row]
    {
        if (safeThis != nullptr) safeThis->session.setCueSamePatch (row, SamePatchBehaviour::keep);
    });
    samePatch.addItem ("Reset to its saved settings", true, reset, [safeThis, row]
    {
        if (safeThis != nullptr) safeThis->session.setCueSamePatch (row, SamePatchBehaviour::reset);
    });

    juce::PopupMenu menu;
    menu.addItem ("Go to this cue", [safeThis, row] { if (safeThis != nullptr) safeThis->session.goToCue (row); });
    menu.addItem ("Rename...", [safeThis, row] { if (safeThis != nullptr) safeThis->rename (row); });
    menu.addSubMenu ("Patch", patches);
    menu.addSubMenu ("If the previous cue uses the same patch", samePatch);
    menu.addSeparator();
    menu.addItem ("Recall with a program change...", [safeThis, row] { if (safeThis != nullptr) safeThis->editProgramChange (row); });
    menu.addItem ("MIDI out when this cue starts...", [safeThis, row] { if (safeThis != nullptr) safeThis->editMidiOut (row); });
    menu.addSeparator();
    menu.addItem ("Remove", [safeThis, row] { if (safeThis != nullptr) safeThis->session.removeCue (row); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void SetList::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (30));

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
