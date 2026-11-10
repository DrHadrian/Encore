// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PatchList.h"

namespace encore
{

PatchList::PatchList (ShowSession& s) : session (s)
{
    header.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    header.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (header);

    list.setRowHeight (38);
    list.setWantsKeyboardFocus (false);   // Space/arrows always step the show (main window)
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1b1e23));
    list.setTooltip ("Click to play a patch, double-click to rename it, right-click for options");
    addAndMakeVisible (list);

    addButton.onClick = [this]
    {
        session.addPatch ("Patch " + juce::String ((int) session.getShow().patches.size() + 1));
    };
    duplicateButton.onClick = [this] { session.duplicatePatch (session.getActivePatchId()); };
    deleteButton.onClick = [this] { confirmDelete(); };

    for (auto* b : { &addButton, &duplicateButton, &deleteButton })
        addAndMakeVisible (b);

    refresh();
}

void PatchList::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    list.updateContent();

    const auto& patches = session.getShow().patches;

    for (int i = 0; i < (int) patches.size(); ++i)
        if (patches[(size_t) i].id == session.getActivePatchId())
            list.selectRow (i, false, true);

    deleteButton.setEnabled (patches.size() > 1);
    list.repaint();
}

int PatchList::getNumRows()
{
    return (int) session.getShow().patches.size();
}

void PatchList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto& patches = session.getShow().patches;

    if (! juce::isPositiveAndBelow (row, (int) patches.size()))
        return;

    const auto& patch = patches[(size_t) row];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 2);

    if (selected)
    {
        g.setColour (juce::Colour (0xff2a3140));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
    }

    g.setColour (juce::Colour (patch.colour));
    g.fillRoundedRectangle (area.removeFromLeft (6).toFloat().reduced (0.0f, 4.0f), 3.0f);

    g.setColour (juce::Colours::white.withAlpha (selected ? 1.0f : 0.8f));
    g.setFont (juce::FontOptions (16.0f, selected ? juce::Font::bold : juce::Font::plain));
    g.drawText (patch.name, area.withTrimmedLeft (10), juce::Justification::centredLeft, true);
}

void PatchList::selectedRowsChanged (int lastRowSelected)
{
    if (updating)
        return;

    const auto& patches = session.getShow().patches;

    if (juce::isPositiveAndBelow (lastRowSelected, (int) patches.size()))
        session.selectPatch (patches[(size_t) lastRowSelected].id);
}

void PatchList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showPatchMenu (row);
}

void PatchList::showPatchMenu (int row)
{
    const auto& patches = session.getShow().patches;

    if (! juce::isPositiveAndBelow (row, (int) patches.size()))
        return;

    const auto& patch = patches[(size_t) row];
    const auto id = patch.id;
    const auto tails = patch.spillover == Spillover::tails;
    juce::Component::SafePointer<PatchList> safeThis (this);

    juce::PopupMenu spill;
    spill.addItem ("Delay and reverb ring out (lighter on CPU)", true, tails, [safeThis, id]
    {
        if (safeThis != nullptr) safeThis->session.setSpillover (id, Spillover::tails);
    });
    spill.addItem ("The whole patch rings out", true, ! tails, [safeThis, id]
    {
        if (safeThis != nullptr) safeThis->session.setSpillover (id, Spillover::fullChain);
    });

    juce::PopupMenu menu;
    menu.addItem ("Rename...", [safeThis, row] { if (safeThis != nullptr) safeThis->rename (row); });
    menu.addItem ("Duplicate", [safeThis, id] { if (safeThis != nullptr) safeThis->session.duplicatePatch (id); });
    menu.addSubMenu ("When switching away from it", spill);
    menu.addSeparator();
    menu.addItem ("Delete...", patches.size() > 1, false, [safeThis] { if (safeThis != nullptr) safeThis->confirmDelete(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void PatchList::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    rename (row);
}

void PatchList::rename (int row)
{
    const auto& patches = session.getShow().patches;

    if (! juce::isPositiveAndBelow (row, (int) patches.size()))
        return;

    const auto id = patches[(size_t) row].id;
    auto* window = new juce::AlertWindow ("Rename patch", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", patches[(size_t) row].name);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PatchList> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window, id] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->session.renamePatch (id, window->getTextEditorContents ("name"));
    }), true);
}

void PatchList::confirmDelete()
{
    const auto* patch = session.getShow().findPatch (session.getActivePatchId());

    if (patch == nullptr)
        return;

    const auto id = patch->id;
    juce::Component::SafePointer<PatchList> safeThis (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions{}
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Delete \"" + patch->name + "\"?")
                                      .withMessage (juce::String ("The patch is removed from this show. A backup of the show is kept.")
                                                    + (session.countCuesUsing (id) > 0
                                                           ? "\n\n" + juce::String (session.countCuesUsing (id))
                                                                 + " cue(s) use it; they will keep their place but no longer change the sound."
                                                           : juce::String()))
                                      .withButton ("Delete", 1)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [safeThis, id] (int result)
                                  {
                                      if (safeThis != nullptr && result == 1)
                                          safeThis->session.deletePatch (id);
                                  });
}

void PatchList::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (30));

    auto buttons = area.removeFromBottom (34);
    const auto w = buttons.getWidth() / 3;
    addButton.setBounds (buttons.removeFromLeft (w).reduced (2));
    duplicateButton.setBounds (buttons.removeFromLeft (w).reduced (2));
    deleteButton.setBounds (buttons.reduced (2));

    area.removeFromBottom (6);
    list.setBounds (area);
}

} // namespace encore
