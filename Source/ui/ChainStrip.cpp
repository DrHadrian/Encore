// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/ChainStrip.h"
#include "session/PatchBuilder.h"

namespace encore
{

namespace
{
    constexpr int tileWidth = 132, tileGap = 28, addWidth = 56;

    const auto tileColour = juce::Colour (0xff1b1e23);
    const auto onColour   = juce::Colour (0xff2e9e5b);
    const auto offColour  = juce::Colour (0xff3a3f47);
    const auto accent     = juce::Colour (0xff4f9dff);
}

ChainStrip::ChainStrip (ShowSession& s) : session (s) {}

void ChainStrip::setChain (Chain* newChain)
{
    chain = newChain;
    const auto count = chain != nullptr ? (int) chain->getBlocks().size() : 0;
    selected = juce::jlimit (0, juce::jmax (0, count - 1), selected);
    resized();
    repaint();
}

void ChainStrip::refresh()
{
    // Repaint only when something visible changed.
    juce::String state;

    if (chain != nullptr)
        for (auto& b : chain->getBlocks())
            state << (b->isEnabled() ? "1" : "0") << b->getStomp() << b->getLoadedFile().getFileName() << "|";

    if (state != lastState)
    {
        lastState = state;
        repaint();
    }
}

void ChainStrip::select (int index)
{
    selected = index;
    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged (index);
}

void ChainStrip::resized()
{
    tiles.clear();
    const auto count = chain != nullptr ? (int) chain->getBlocks().size() : 0;
    auto area = getLocalBounds().reduced (0, 8);
    int x = 0;

    for (int i = 0; i < count; ++i)
    {
        tiles.push_back ({ x, area.getY(), tileWidth, area.getHeight() });
        x += tileWidth + tileGap;
    }

    addTile = { x, area.getY() + area.getHeight() / 2 - addWidth / 2, addWidth, addWidth };
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (chain == nullptr)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Loading patch...", getLocalBounds(), juce::Justification::centredLeft);
        return;
    }

    const auto& blocks = chain->getBlocks();

    for (size_t i = 0; i < tiles.size() && i < blocks.size(); ++i)
    {
        const auto& block = *blocks[i];
        const auto r = tiles[i].toFloat();
        const auto isSelected = (int) i == selected;

        // Arrow to the next tile (or to "+").
        const auto arrowY = r.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawArrow ({ r.getRight() + 4.0f, arrowY, r.getRight() + tileGap - 4.0f, arrowY }, 1.5f, 8.0f, 8.0f);

        g.setColour (tileColour);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (isSelected ? accent : (block.isEnabled() ? onColour : offColour));
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, isSelected ? 3.0f : 2.0f);

        auto text = tiles[i].reduced (12, 10);
        g.setColour (juce::Colours::white.withAlpha (block.isEnabled() ? 1.0f : 0.45f));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (block.getName(), text.removeFromTop (24), juce::Justification::centredLeft, true);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        const auto file = block.getLoadedFile();
        const auto detail = file != juce::File() ? file.getFileNameWithoutExtension()
                                                  : (block.getFileSlot() != nullptr ? juce::String ("(empty)") : juce::String());
        g.drawFittedText (detail, text.removeFromTop (34), juce::Justification::topLeft, 2);

        g.setColour (block.isEnabled() ? onColour : offColour);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        auto bottom = text.removeFromBottom (16);
        g.drawText (block.isEnabled() ? "ON" : "OFF", bottom, juce::Justification::bottomLeft);

        if (const auto stomp = block.getStomp(); stomp > 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawText ("STOMP " + juce::String (stomp), bottom, juce::Justification::bottomRight);
        }
    }

    // "+" to add a block at the end.
    g.setColour (tileColour);
    g.fillEllipse (addTile.toFloat());
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawEllipse (addTile.toFloat().reduced (1.0f), 1.5f);
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("+", addTile, juce::Justification::centred);
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    if (chain == nullptr)
        return;

    if (addTile.contains (e.getPosition()))
    {
        showAddMenu ((int) tiles.size());
        return;
    }

    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        if (tiles[(size_t) i].contains (e.getPosition()))
        {
            select (i);

            if (e.mods.isPopupMenu())
                showBlockMenu (i);

            return;
        }
    }
}

juce::PopupMenu ChainStrip::blockTypeMenu (int insertAt)
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<ChainStrip> safeThis (this);

    for (auto& type : getBlockTypes())
        menu.addItem (type.displayName, [safeThis, id = type.id, insertAt]
        {
            if (safeThis != nullptr)
            {
                safeThis->selected = insertAt;   // the new block is selected once built
                safeThis->session.addBlock (id, insertAt);
            }
        });

    return menu;
}

void ChainStrip::showAddMenu (int insertAt)
{
    blockTypeMenu (insertAt).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                                        .withTargetScreenArea (localAreaToGlobal (addTile)));
}

void ChainStrip::showBlockMenu (int index)
{
    const auto count = (int) tiles.size();
    juce::Component::SafePointer<ChainStrip> safeThis (this);
    juce::PopupMenu menu;

    menu.addItem ("Move left", index > 0, false, [safeThis, index]
    {
        if (safeThis != nullptr) { safeThis->selected = index - 1; safeThis->session.moveBlock (index, index - 1); }
    });
    menu.addItem ("Move right", index < count - 1, false, [safeThis, index]
    {
        if (safeThis != nullptr) { safeThis->selected = index + 1; safeThis->session.moveBlock (index, index + 1); }
    });
    menu.addSeparator();

    // Which footswitch turns this block on and off (in this patch).
    juce::PopupMenu stomps;
    const auto current = session.getBlockStomp (index);

    for (int n = 0; n <= maxStomps; ++n)
        stomps.addItem (n == 0 ? juce::String ("None") : "Stomp " + juce::String (n), true, n == current, [safeThis, index, n]
        {
            if (safeThis != nullptr) { safeThis->session.setBlockStomp (index, n); safeThis->repaint(); }
        });

    menu.addSubMenu ("Stomp switch", stomps);
    menu.addSeparator();
    menu.addSubMenu ("Insert before", blockTypeMenu (index));
    menu.addSubMenu ("Insert after", blockTypeMenu (index + 1));
    menu.addSeparator();
    menu.addItem ("Remove", [safeThis, index]
    {
        if (safeThis != nullptr) safeThis->session.removeBlock (index);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (tiles[(size_t) index])));
}

} // namespace encore
