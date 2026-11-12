// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/ChainStrip.h"
#include "session/PatchBuilder.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/blocks/PluginBlock.h"
#include "session/PluginCatalog.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

ChainStrip::ChainStrip (ShowSession& s) : session (s)
{
    setHeader ("Signal chain", juce::String (juce::CharPointer_UTF8 ("Click a block to edit it \xc2\xb7 right-click to move, insert, remove or give it a stomp switch")));
    setTitle ("Signal chain");
}

kit::BlockInfo ChainStrip::describe (const Block& block)
{
    using Shape = kit::BlockInfo::Shape;
    const auto& type = block.getTypeId();
    kit::BlockInfo info;
    info.shape = type == NamAmpBlock::typeId ? Shape::amp : type == CabIrBlock::typeId ? Shape::cab : Shape::pedal;
    info.kindLabel = type == NamAmpBlock::typeId    ? juce::String ("Amp")
                   : type == CabIrBlock::typeId     ? juce::String ("Cab")
                   : type == NoiseGateBlock::typeId ? juce::String ("Gate")
                   : type == PluginBlock::typeId    ? juce::String ("Plugin")
                                                    : block.getName();   // "EQ", "Delay"
    info.name = block.getName();
    info.on = block.isEnabled();
    return info;
}

void ChainStrip::setChain (Chain* newChain)
{
    chain = newChain;
    const auto count = chain != nullptr ? (int) chain->getBlocks().size() : 0;
    selected = juce::jlimit (0, juce::jmax (0, count - 1), selected);
    rebuildTiles();
    resized();
    repaint();
}

kit::UtilityTileInfo ChainStrip::describePlugin (const Block& block)
{
    kit::UtilityTileInfo info;
    info.footswitch = true;

    if (auto* plugin = dynamic_cast<const PluginBlock*> (&block))
    {
        const auto& ref = plugin->getPluginRef();
        info.label = ref.format.isNotEmpty() ? ref.format : juce::String ("VST3");
        info.name = ref.name.isNotEmpty() ? ref.name : block.getName();
        info.caption = ref.maker;

        // Not loaded or switched off by the crash guard: amber, as on the stage.
        info.amber = ! plugin->isLoaded() || plugin->getFault() != PluginBlock::Fault::none;
    }
    else
    {
        info.name = block.getName();
    }

    using Dot = kit::UtilityTileInfo::Dot;
    info.dot = info.amber ? Dot::amber : (block.isEnabled() ? Dot::on : Dot::off);
    return info;
}

void ChainStrip::rebuildTiles()
{
    views.clear();

    if (chain == nullptr)
        return;

    for (auto& b : chain->getBlocks())
    {
        TileView view;

        if (b->getTypeId() == PluginBlock::typeId)
            view.utility = std::make_unique<kit::UtilityTile> (describePlugin (*b));
        else
            view.gear = std::make_unique<kit::GearPlaceholder> (kit::GearView::Size::chainTile, describe (*b));

        view.component().setTitle (b->getName());
        addAndMakeVisible (view.component());
        views.push_back (std::move (view));
    }
}

void ChainStrip::updateTile (TileView& view, const Block& block)
{
    if (view.gear != nullptr)
        view.gear->setBlock (describe (block));
    else
        view.utility->setInfo (describePlugin (block));
}

void ChainStrip::refresh()
{
    // Repaint only when something visible changed.
    juce::String state;

    if (chain != nullptr)
    {
        for (auto& b : chain->getBlocks())
        {
            state << (b->isEnabled() ? "1" : "0") << b->getStomp() << b->getLoadedFile().getFileName();

            if (auto* plugin = dynamic_cast<const PluginBlock*> (b.get()))
                state << (plugin->isLoaded() ? "L" : "-") << (int) plugin->getFault();

            state << "|";
        }
    }

    if (state != lastState)
    {
        lastState = state;

        if (chain != nullptr)
            for (size_t i = 0; i < chain->getBlocks().size() && i < views.size(); ++i)
                updateTile (views[i], *chain->getBlocks()[i]);

        repaint();
    }
}

void ChainStrip::selectBlock (int index)
{
    if (juce::isPositiveAndBelow (index, (int) tiles.size()))
        select (index);
}

void ChainStrip::select (int index)
{
    selected = index;
    repaint();

    if (onSelectionChanged != nullptr)
        onSelectionChanged (index);
}

juce::Rectangle<int> ChainStrip::getBoardBounds() const
{
    return getContentBounds();
}

void ChainStrip::resized()
{
    Pane::resized();

    // Bottom-aligned on the board, 6 px apart, 14 × 12 padding.
    tiles.clear();
    const auto board = getBoardBounds().reduced (size::chainPadX, size::chainPadY);
    auto x = board.getX();

    for (auto& view : views)
    {
        const auto r = view.gear != nullptr ? kit::GearView::idealBounds (kit::GearView::Size::chainTile, view.gear->getBlock().shape)
                                            : juce::Rectangle<int> (size::tileW, size::utilityTileH);
        tiles.push_back (r.withPosition (x, board.getBottom() - r.getHeight()));
        view.component().setBounds (tiles.back());
        x += r.getWidth() + size::tileGap;
    }

    addTile = { x, board.getBottom() - size::tileH, size::plusTileW, size::tileH };
    addTileOutline.clear();

    // The "+" tile: 2 px dashed border, radius 8.
    juce::Path rounded;
    rounded.addRoundedRectangle (addTile.toFloat().reduced (1.0f), (float) size::plusTileRadius - 1.0f);
    const float dashes[] = { (float) size::dash, (float) size::dash };
    juce::PathStrokeType ((float) size::plusTileStroke).createDashedStroke (addTileOutline, rounded, dashes, 2);
}

void ChainStrip::paint (juce::Graphics& g)
{
    Pane::paint (g);

    const auto board = getBoardBounds();
    g.setColour (paint::c (colour::chainBoard));
    g.fillRect (board);

    // Inset shadow along the top (0 2px 6px black 70 %).
    g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::chainBoardShadow), (float) board.getY(),
                                                       paint::c (colour::chainBoardShadow).withAlpha (0.0f), (float) (board.getY() + size::chainShadowH)));
    g.fillRect (board.withHeight (size::chainShadowH));

    if (chain == nullptr)
    {
        paint::text (g, "Loading patch...", board.reduced (size::chainPadX, 0).toFloat(), type::body, colour::textMuted);
        return;
    }

    // Shadows under the pedals, then the selection outline (3 px, 3 px away).
    for (auto& view : views)
    {
        if (view.utility != nullptr)
            kit::UtilityTile::paintShadow (g, view.utility->getBounds().toFloat());
        else if (view.gear->getBlock().shape == kit::BlockInfo::Shape::pedal)
            kit::GearPlaceholder::paintPedalShadow (g, view.gear->getBounds().toFloat());
    }

    // The "+" tile.
    g.setColour (paint::c (colour::dashedTile));
    g.fillPath (addTileOutline);
    paint::text (g, "+", addTile.toFloat(), type::plusGlyph, colour::plusTile, juce::Justification::centred);
}

void ChainStrip::paintOverChildren (juce::Graphics& g)
{
    if (juce::isPositiveAndBelow (selected, (int) tiles.size()))
    {
        const auto r = tiles[(size_t) selected].toFloat().expanded ((float) size::selectionOffset + (float) size::selectionWidth * 0.5f);
        g.setColour (paint::c (colour::tileSelection));
        g.drawRoundedRectangle (r, (float) size::selectionRadius, (float) size::selectionWidth);
    }
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

    // Plugins, by maker (Settings > Plugins finds them).
    menu.addSeparator();
    juce::PopupMenu plugins;
    const auto catalog = session.getPluginCatalog();
    const auto found = catalog != nullptr ? catalog->getPlugins() : juce::Array<juce::PluginDescription>();
    std::map<juce::String, juce::PopupMenu> byMaker;

    for (const auto& d : found)
        byMaker[d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other")]
            .addItem (d.name, [safeThis, ref = PluginCatalog::toRef (d), insertAt]
            {
                if (safeThis != nullptr)
                {
                    safeThis->selected = insertAt;
                    safeThis->session.addPluginBlock (ref, insertAt);
                }
            });

    // Makers A to Z, then "Other" (plugins that don't name one).
    for (auto& [maker, items] : byMaker)
        if (maker != "Other")
            plugins.addSubMenu (maker, items);

    if (const auto other = byMaker.find ("Other"); other != byMaker.end())
        plugins.addSubMenu (other->first, other->second);

    if (found.isEmpty())
        plugins.addItem ("No plugins found", false, false, nullptr);

    plugins.addSeparator();
    plugins.addItem ("Find plugins...", [safeThis]
    {
        if (safeThis != nullptr && safeThis->onFindPlugins != nullptr)
            safeThis->onFindPlugins();
    });

    menu.addSubMenu ("Plugin", plugins);
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
