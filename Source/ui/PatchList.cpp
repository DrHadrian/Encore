// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PatchList.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

PatchList::PatchList (ShowSession& s) : session (s)
{
    setHeader ("Patches");
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
    list.updateContent();

    const auto& patches = session.getShow().patches;

    for (int i = 0; i < (int) patches.size(); ++i)
        if (patches[(size_t) i].id == session.getActivePatchId())
            list.scrollToEnsureRowIsOnscreen (i);

    deleteButton.setEnabled (patches.size() > 1);
}

int PatchList::getNumRows()
{
    return (int) session.getShow().patches.size();
}

int PatchList::getRowHeight (int)
{
    return size::rowHeight;
}

void PatchList::paintRow (juce::Graphics& g, int row, juce::Rectangle<int> bounds)
{
    const auto& patches = session.getShow().patches;

    if (! juce::isPositiveAndBelow (row, (int) patches.size()))
        return;

    const auto& patch = patches[(size_t) row];
    const auto selected = patch.id == session.getActivePatchId();

    if (selected)
    {
        g.setColour (paint::c (colour::patchSelected));
        g.fillRoundedRectangle (bounds.toFloat(), (float) size::rowRadius);
    }

    // Colour swatch, name (600 when selected).
    auto area = bounds.toFloat().reduced ((float) size::rowPadX, 0.0f);
    const auto swatch = area.removeFromLeft ((float) size::swatch).withSizeKeepingCentre ((float) size::swatch, (float) size::swatch);
    g.setColour (juce::Colour (patch.colour));   // the patch's own colour (show data)
    g.fillRoundedRectangle (swatch, (float) size::swatchRadius);
    area.removeFromLeft ((float) size::rowGap);
    paint::text (g, patch.name, area, selected ? type::rowStrong : type::body, colour::textRow);
}

void PatchList::rowClicked (int row, const juce::MouseEvent& e)
{
    // As before: a click (also a right-click) selects the patch and plays it.
    const auto& patches = session.getShow().patches;

    if (! juce::isPositiveAndBelow (row, (int) patches.size()))
        return;

    if (patches[(size_t) row].id != session.getActivePatchId())
        session.selectPatch (patches[(size_t) row].id);

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

    // Switch feel: how quickly it comes in, how long its amp overlaps the next patch.
    const auto fadeIn = patch.fadeInMs, ringOut = patch.ringOutMs;
    juce::PopupMenu fadeInMenu, ringOutMenu;

    for (auto ms : { 5.0f, 10.0f, 20.0f, 50.0f, 100.0f })
        fadeInMenu.addItem (juce::String ((int) ms) + " ms", true, juce::approximatelyEqual (ms, fadeIn), [safeThis, id, ms, ringOut]
        {
            if (safeThis != nullptr) safeThis->session.setSwitchFades (id, ms, ringOut);
        });

    for (auto ms : { 10.0f, 50.0f, 150.0f, 300.0f, 500.0f, 1000.0f })
        ringOutMenu.addItem (juce::String ((int) ms) + " ms", true, juce::approximatelyEqual (ms, ringOut), [safeThis, id, ms, fadeIn]
        {
            if (safeThis != nullptr) safeThis->session.setSwitchFades (id, fadeIn, ms);
        });

    juce::PopupMenu menu;
    menu.addItem ("Rename...", [safeThis, row] { if (safeThis != nullptr) safeThis->rename (row); });
    menu.addItem ("Duplicate", [safeThis, id] { if (safeThis != nullptr) safeThis->session.duplicatePatch (id); });
    menu.addSeparator();
    menu.addSubMenu ("Fade in when switched to (" + juce::String ((int) fadeIn) + " ms)", fadeInMenu);
    menu.addSubMenu ("Amp ring-out when switched away (" + juce::String ((int) ringOut) + " ms)", ringOutMenu);
    menu.addSubMenu ("Then let ring out", spill);
    menu.addSeparator();
    menu.addItem ("Delete...", patches.size() > 1, false, [safeThis] { if (safeThis != nullptr) safeThis->confirmDelete(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void PatchList::rowDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
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

juce::Rectangle<int> PatchList::getFooterBounds() const
{
    // 11 px, line height 1.45, padding 8 × 10, a line above (two lines of text).
    const auto lineH = (int) std::ceil (type::caption.px * 1.45f);
    return getContentBounds().removeFromBottom (size::paneFooterPadY * 2 + lineH * 2 + 1);
}

void PatchList::paint (juce::Graphics& g)
{
    Pane::paint (g);

    const auto f = getFooterBounds();
    g.setColour (paint::c (colour::paneHeaderLine));
    paint::hline (g, (float) f.getX(), (float) f.getY(), (float) f.getWidth());
    g.setColour (paint::c (colour::textMuted));
    g.setFont (font (type::caption));
    g.drawFittedText (footer, f.withTrimmedTop (1).reduced (size::paneFooterPadX, size::paneFooterPadY),
                      juce::Justification::topLeft, 3, 1.0f);
}

void PatchList::resized()
{
    Pane::resized();

    auto header = getHeaderBounds().withTrimmedRight (size::paneHeaderPadR);

    for (auto* b : { &deleteButton, &duplicateButton, &addButton })
    {
        b->setBounds (header.removeFromRight (size::iconButton).withSizeKeepingCentre (size::iconButton, size::iconButton));
        header.removeFromRight (size::headerButtonGap);
    }

    list.setBounds (getContentBounds().withTrimmedBottom (getFooterBounds().getHeight()));
}

} // namespace encore
