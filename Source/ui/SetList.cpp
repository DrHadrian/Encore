// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/SetList.h"
#include "ui/CueDialogs.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

SetList::SetList (ShowSession& s) : session (s)
{
    setVariant (expandButton, Variant::small);
    expandButton.setTooltip ("The full list: sections, bar marks, notes, cut items, fast typing (F6)");
    expandButton.onClick = [this] { if (onExpand != nullptr) onExpand(); };
    addAndMakeVisible (expandButton);

    addAndMakeVisible (list);

    // A new item plays the current patch, right after the playing one.
    addButton.onClick = [this] { session.addCue (session.getActivePatchId(), session.getCurrentCue()); };
    removeButton.onClick = [this] { session.removeCue (cueIndexOfRow (selectedRow)); };

    for (auto [button, direction] : { std::pair { &upButton, -1 }, std::pair { &downButton, 1 } })
    {
        button->onClick = [this, direction = direction]
        {
            if (! juce::isPositiveAndBelow (selectedRow, (int) rows.size()))
                return;

            const auto id = rows[(size_t) selectedRow].id;
            session.moveItem (id, direction);   // steps in and out of sections too
            refresh();

            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(size_t) i].id == id)
                    selectRow (i);
        };
    }

    for (auto* b : { &addButton, &removeButton, &upButton, &downButton })
        addAndMakeVisible (b);

    refresh();
}

void SetList::refresh()
{
    const auto words = session.getWords();

    setHeader (words.list);
    list.setTooltip ("Click to go to a " + words.item + ". Space or arrow keys step through.\n"
                     "Double-click to rename, right-click for options.");
    addButton.setTooltip ("New " + words.item + " after the playing one");
    addButton.setTitle ("New " + words.item + " after the playing one");
    removeButton.setTooltip ("Remove " + words.item);
    removeButton.setTitle ("Remove " + words.item);
    upButton.setTooltip ("Move " + words.item + " up");
    upButton.setTitle ("Move " + words.item + " up");
    downButton.setTooltip ("Move " + words.item + " down");
    downButton.setTitle ("Move " + words.item + " down");

    // Sections as headings, playable items; cut items only show in the full list.
    const auto oldSelectedId = juce::isPositiveAndBelow (selectedRow, (int) rows.size()) ? rows[(size_t) selectedRow].id : juce::String();
    rows.clear();

    for (auto& r : session.getRunningOrderRows())
    {
        if (r.item->kind == RunItem::Kind::section)
            rows.push_back ({ r.item->id, -1, r.item->name.toUpperCase() });
        else if (r.cueIndex >= 0)
            rows.push_back ({ r.item->id, r.cueIndex, {} });
    }

    list.updateContent();

    // The playing item is selected (as before); otherwise keep the selection.
    selectedRow = -1;
    const auto current = session.getCurrentCue();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (current >= 0 && rows[(size_t) i].cueIndex == current)
        {
            selectedRow = i;
            list.scrollToEnsureRowIsOnscreen (i);
            break;
        }

        if (oldSelectedId.isNotEmpty() && rows[(size_t) i].id == oldSelectedId)
            selectedRow = i;
    }

    updateButtons();
}

void SetList::selectRow (int row)
{
    if (row != selectedRow)
    {
        list.repaintRow (selectedRow);
        selectedRow = row;
        list.repaintRow (selectedRow);
        list.scrollToEnsureRowIsOnscreen (selectedRow);
        updateButtons();
    }
}

int SetList::cueIndexOfRow (int row) const
{
    return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row].cueIndex : -1;
}

void SetList::updateButtons()
{
    const auto row = selectedRow;
    const auto hasSelection = juce::isPositiveAndBelow (row, (int) rows.size());
    removeButton.setEnabled (hasSelection && rows[(size_t) row].cueIndex >= 0);
    upButton.setEnabled (hasSelection);
    downButton.setEnabled (hasSelection);
}

int SetList::getNumRows()
{
    return (int) rows.size();
}

int SetList::getRowHeight (int row)
{
    return juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].cueIndex < 0 ? size::sectionHeadingH : size::rowHeight;
}

void SetList::paintRow (juce::Graphics& g, int row, juce::Rectangle<int> bounds)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    auto area = bounds.toFloat().reduced ((float) size::rowPadX, 0.0f);

    if (rows[(size_t) row].cueIndex < 0)
    {
        // A section heading (act, scene, set...): amber caps, at the bottom.
        paint::text (g, rows[(size_t) row].heading, area.withTrimmedBottom ((float) size::headingPadBottom),
                     type::sectionHeading, colour::amberSection, juce::Justification::bottomLeft);
        return;
    }

    const auto cues = session.getCues();
    const auto index = rows[(size_t) row].cueIndex;

    if (! juce::isPositiveAndBelow (index, (int) cues.size()))
        return;

    const auto& cue = *cues[(size_t) index];
    const auto playing = index == session.getCurrentCue();
    const auto selected = row == selectedRow;

    if (playing || selected)
    {
        g.setColour (paint::c (playing ? colour::selectedRow : colour::patchSelected));
        g.fillRoundedRectangle (bounds.toFloat(), (float) size::rowRadius);
    }

    // Playing bar, number, name ... patch (PC, OUT).
    const auto bar = area.removeFromLeft ((float) size::paneBarW);

    if (playing)
    {
        g.setColour (paint::c (colour::healthy));
        g.fillRoundedRectangle (bar.withSizeKeepingCentre (bar.getWidth(), (float) size::paneBarH), (float) size::paneBarRadius);
    }

    area.removeFromLeft ((float) size::rowGap);
    paint::text (g, cue.number, area.removeFromLeft ((float) size::rowNumberW), type::body,
                 playing ? colour::textStrong : colour::textDim);
    area.removeFromLeft ((float) size::rowGap);

    const auto* patch = cue.uniquePatch ? &*cue.uniquePatch : session.getShow().findPatch (cue.patchId);
    const auto patchText = patch != nullptr ? patch->name : juce::String ("(patch missing)");
    const auto midiText = juce::String (cue.programChange >= 0 ? "PC " + juce::String (cue.programChange) + "  " : "")
                        + (cue.midiOut.empty() ? "" : "OUT  ");
    const auto trailing = midiText + patchText;
    const auto trailingW = juce::jmin (area.getWidth() * 0.5f, textWidth (font (type::rowTrailing), trailing) + 1.0f);
    paint::text (g, trailing, area.removeFromRight (trailingW), type::rowTrailing,
                 patch == nullptr ? colour::amber : playing ? colour::selectedRowSub : colour::textMuted,
                 juce::Justification::centredRight);
    area.removeFromRight ((float) size::rowGap);

    paint::text (g, cue.name.isNotEmpty() ? cue.name : patchText, area, playing ? type::rowStrong : type::body, colour::text);
}

void SetList::rowClicked (int row, const juce::MouseEvent& e)
{
    const auto index = cueIndexOfRow (row);
    selectRow (row);   // a heading too: Up / Down move whole sections

    if (index < 0)
        return;

    if (e.mods.isPopupMenu())
        showCueMenu (index);
    else
        session.goToCue (index);
}

void SetList::rowDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

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
    Pane::resized();

    // Header: Full list, then + − ↑ ↓ (1 px apart), right-aligned.
    auto header = getHeaderBounds().withTrimmedRight (size::paneHeaderPadR);

    for (auto* b : { &downButton, &upButton, &removeButton, &addButton })
    {
        b->setBounds (header.removeFromRight (size::iconButton).withSizeKeepingCentre (size::iconButton, size::iconButton));
        header.removeFromRight (size::headerButtonGap);
    }

    header.removeFromRight (size::headerSmallButtonMargin);
    const auto w = expandButton.getBestWidthForHeight (size::buttonSmallHeight);
    expandButton.setBounds (header.removeFromRight (w).withTrimmedTop ((header.getHeight() - size::buttonSmallHeight) / 2)
                                                      .withHeight (size::buttonSmallHeight + size::buttonDrop));

    list.setBounds (getContentBounds());
}

} // namespace encore
