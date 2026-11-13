// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/ChainStrip.h"
#include "session/PatchBuilder.h"

#include "engine/blocks/CabIrBlock.h"
#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/blocks/PluginBlock.h"
#include "engine/blocks/SplitBlocks.h"
#include "session/PluginCatalog.h"
#include "ui/kit/Chip.h"
#include "ui/theme/Paint.h"

namespace encore
{

namespace
{
    juce::String hintFor (bool split)
    {
        return juce::String (juce::CharPointer_UTF8 (split ? "Click a block to edit it \xc2\xb7 right-click to move it between paths A and B, insert, remove or give it a stomp switch"
                                                           : "Click a block to edit it \xc2\xb7 right-click to move, insert, remove or give it a stomp switch"));
    }
} // namespace

using namespace theme;

ChainStrip::ChainStrip (ShowSession& s) : session (s)
{
    setHeader ("Signal chain", hintFor (false));
    setTitle ("Signal chain");

    for (auto* jack : { &inTile, &outTile })
        addAndMakeVisible (jack);

    inTile.setTitle ("Input");
    outTile.setTitle ("Outputs");
}

void ChainStrip::setJackTiles (const kit::UtilityTileInfo& in, const kit::UtilityTileInfo& out)
{
    inTile.setInfo (in);
    outTile.setInfo (out);
}

int ChainStrip::getIdealHeight() const
{
    return chain != nullptr && chain->hasSplit() ? size::chainSplitPaneH : size::chainPaneH;
}

int ChainStrip::rowOf (int index) const
{
    if (chain == nullptr || ! chain->hasSplit())
        return 0;

    if (index >= chain->getNumMainBlocks())
        return 2;

    return index > chain->getSplitIndex() && index < chain->getMergeIndex() ? 1 : 0;
}

juce::Rectangle<int> ChainStrip::selectedBounds() const
{
    if (selected == inputSelected)
        return inTile.getBounds();

    if (selected == outputSelected)
        return outTile.getBounds();

    return juce::isPositiveAndBelow (selected, (int) tiles.size()) ? tiles[(size_t) selected] : juce::Rectangle<int>();
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

    if (selected != inputSelected && selected != outputSelected)
        selected = juce::jlimit (0, juce::jmax (0, count - 1), selected);

    rebuildTiles();
    resized();
    repaint();

    // Two rows need a taller pane.
    const auto split = chain != nullptr && chain->hasSplit();

    if (split != hadSplit)
    {
        hadSplit = split;
        setHeader ("Signal chain", hintFor (split));   // "between paths A and B" only when there are paths

        if (onHeightChanged != nullptr)
            onHeightChanged();
    }
}

kit::UtilityTileInfo ChainStrip::describePlugin (const Block& block)
{
    kit::UtilityTileInfo info;

    // Split and merge (milestone 11): a narrow tile, its state at the bottom.
    if (block.getTypeId() == SplitBlock::typeId)
    {
        info.label = "Split";
        info.name = block.isEnabled() ? "A + B" : "A only";
        info.nameAtBottom = true;
        info.icon = Icon::split;
        return info;
    }

    if (block.getTypeId() == MergeBlock::typeId)
    {
        info.label = "Merge";
        info.name = "stereo";
        info.nameAtBottom = true;
        info.icon = Icon::merge;
        return info;
    }

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

        if (b->getTypeId() == PluginBlock::typeId || b->getTypeId() == SplitBlock::typeId || b->getTypeId() == MergeBlock::typeId)
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

        resized();   // path B dims while the split has it off
        repaint();
    }
}

void ChainStrip::selectBlock (int index)
{
    if (juce::isPositiveAndBelow (index, (int) tiles.size()) || index == inputSelected || index == outputSelected)
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

    // IN, the blocks, "+", 6 px apart; OUT at the right end. Without split
    // paths the tiles stand on the board's bottom (14 × 12 padding); with
    // them the main line is centred between the two path rows.
    tiles.assign (chain != nullptr ? chain->getBlocks().size() : 0, {});
    pathRows[0] = pathRows[1] = {};
    const auto content = getBoardBounds();
    const auto board = content.reduced (size::chainPadX, size::chainPadY);
    const auto split = chain != nullptr && chain->hasSplit();
    const auto mainY = split ? content.getCentreY() - size::tileH / 2 + 1 : board.getBottom() - size::tileH;

    auto sizeOf = [this] (size_t i)
    {
        const auto& view = views[i];
        return view.gear != nullptr ? kit::GearView::idealBounds (kit::GearView::Size::chainTile, view.gear->getBlock().shape)
                                    : juce::Rectangle<int> (chain->getBlocks()[i]->getTypeId() == PluginBlock::typeId ? size::tileW : size::splitTileW,
                                                            size::utilityTileH);
    };

    auto x = board.getX();
    inTile.setBounds (x, mainY, size::jackTileW, size::utilityTileH);
    x += size::jackTileW + size::tileGap;

    const auto numMain = chain != nullptr ? (size_t) chain->getNumMainBlocks() : views.size();

    for (size_t i = 0; i < numMain && i < views.size(); ++i)
    {
        const auto r = sizeOf (i);

        if (rowOf ((int) i) == 1)
            continue;   // path A: laid out with its row

        // Path rows between the split and the merge.
        if (split && (int) i == chain->getMergeIndex())
        {
            const auto rowsX = x - size::tileGap + size::pathRowGap;
            auto widthOf = [&] (int row)
            {
                auto w = 0;

                for (size_t j = 0; j < views.size(); ++j)
                    if (rowOf ((int) j) == row)
                        w += sizeOf (j).getWidth() + size::tileGap;

                return juce::jmax (size::tileW, w - size::tileGap);
            };

            const auto rowW = size::pathRowLead + juce::jmax (widthOf (1), widthOf (2)) + size::pathRowPadRight;
            pathRows[0] = { rowsX, mainY - size::pathAbove, rowW, size::pathRowH };
            pathRows[1] = pathRows[0].translated (0, size::pathRowPitch);

            for (int row = 1; row <= 2; ++row)
            {
                auto px = pathRows[row - 1].getX() + size::pathRowLead;

                for (size_t j = 0; j < views.size(); ++j)
                {
                    if (rowOf ((int) j) != row)
                        continue;

                    const auto pr = sizeOf (j);
                    tiles[j] = pr.withPosition (px, pathRows[row - 1].getY() + size::pathTileTop);
                    px += pr.getWidth() + size::tileGap;
                }
            }

            x = pathRows[0].getRight() + size::pathRowGap;
        }

        tiles[i] = r.withPosition (x, mainY + size::tileH - r.getHeight());
        x += r.getWidth() + size::tileGap;
    }

    for (size_t i = 0; i < views.size(); ++i)
    {
        views[i].component().setBounds (tiles[i]);

        // Path B, while the split has it off: dimmed.
        const auto dim = split && rowOf ((int) i) == 2 && ! chain->getBlocks()[(size_t) chain->getSplitIndex()]->isEnabled();
        views[i].component().setAlpha (dim ? state::disabledAlpha : 1.0f);
    }

    addTile = { x, mainY, size::plusTileW, size::tileH };
    outTile.setBounds (content.getRight() - size::outTileRight - size::outTileW, mainY, size::outTileW, size::utilityTileH);
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

    // Split paths: two rows, A over B, each with its badge.
    for (int row = 0; row < 2; ++row)
    {
        const auto r = pathRows[row].toFloat();

        if (r.isEmpty())
            continue;

        g.setColour (paint::c (colour::pathRow));
        g.fillRoundedRectangle (r, (float) size::pathRowRadius);
        g.setColour (paint::c (colour::pathRowBorder));
        g.drawRoundedRectangle (r.reduced (0.5f), (float) size::pathRowRadius, 1.0f);

        const auto off = row == 1 && chain->hasSplit() && ! chain->getBlocks()[(size_t) chain->getSplitIndex()]->isEnabled();
        const auto badge = juce::Rectangle<float> ((float) size::pathBadge, (float) size::pathBadge)
                               .withCentre ({ r.getX() + (float) size::pathBadgeX + (float) size::pathBadge * 0.5f, r.getCentreY() });
        g.setColour (paint::c (off ? colour::pathBadgeOffBg : colour::pathBadgeBg));
        g.fillEllipse (badge);
        paint::text (g, row == 0 ? "A" : "B", badge, type::pathBadge, off ? colour::textDisabled : colour::text, juce::Justification::centred);
    }

    for (auto* jack : { &inTile, &outTile })
        kit::UtilityTile::paintShadow (g, jack->getBounds().toFloat());

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
    // Path B while the split sends to A only: OFF, on the row's top edge.
    if (chain != nullptr && ! pathRows[1].isEmpty() && chain->hasSplit()
        && ! chain->getBlocks()[(size_t) chain->getSplitIndex()]->isEnabled())
    {
        const auto text = juce::String ("OFF");
        const auto w = kit::chipWidth (text);
        const auto r = pathRows[1].toFloat();
        kit::paintChip (g, { r.getRight() - (float) size::pathOffRight - w, r.getY(), w, (float) size::tagChipH }, text, kit::ChipStyle::off);
    }

    if (const auto bounds = selectedBounds(); ! bounds.isEmpty())
    {
        const auto r = bounds.toFloat().expanded ((float) size::selectionOffset + (float) size::selectionWidth * 0.5f);
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
        showAddMenu (chain->getNumMainBlocks());
        return;
    }

    if (inTile.getBounds().contains (e.getPosition()))
    {
        select (inputSelected);
        return;
    }

    if (outTile.getBounds().contains (e.getPosition()))
    {
        select (outputSelected);
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

    // An empty spot on a path row: add a block to that path.
    for (int row = 0; row < 2; ++row)
        if (pathRows[row].contains (e.getPosition()) && e.mods.isPopupMenu())
            showPathMenu (row);
}

void ChainStrip::showPathMenu (int row)
{
    if (chain == nullptr || ! chain->hasSplit())
        return;

    // Path A ends just before the merge (main lane); path B at its end (lane 1).
    const auto lane = row == 0 ? 0 : 1;
    const auto at = row == 0 ? chain->getMergeIndex() : (int) chain->getBlocks().size() - chain->getNumMainBlocks();
    juce::PopupMenu menu;
    menu.addSubMenu (row == 0 ? "Add to path A" : "Add to path B", blockTypeMenu (at, lane));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (pathRows[row])));
}

juce::PopupMenu ChainStrip::blockTypeMenu (int insertAt, int lane)
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<ChainStrip> safeThis (this);

    // The new block is selected once built: its index in the chain.
    const auto flatIndex = lane == 0 || chain == nullptr ? insertAt : chain->getNumMainBlocks() + insertAt;

    for (auto& type : getBlockTypes())
        menu.addItem (type.displayName, [safeThis, id = type.id, insertAt, lane, flatIndex]
        {
            if (safeThis != nullptr)
            {
                safeThis->selected = flatIndex;
                safeThis->session.addBlock (id, insertAt, lane);
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
            .addItem (d.name, [safeThis, ref = PluginCatalog::toRef (d), insertAt, lane, flatIndex]
            {
                if (safeThis != nullptr)
                {
                    safeThis->selected = flatIndex;
                    safeThis->session.addPluginBlock (ref, insertAt, lane);
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

    // Split paths (milestone 11).
    const auto row = rowOf (index);
    const auto& type = chain->getBlocks()[(size_t) index]->getTypeId();
    const auto isSplitOrMerge = type == SplitBlock::typeId || type == MergeBlock::typeId;
    menu.addSeparator();

    if (row == 1)
        menu.addItem ("Move to path B", [safeThis, index] { if (safeThis != nullptr) safeThis->session.moveBlockToPath (index, true); });
    else if (row == 2)
        menu.addItem ("Move to path A", [safeThis, index] { if (safeThis != nullptr) safeThis->session.moveBlockToPath (index, false); });
    else if (isSplitOrMerge)
        menu.addItem ("Remove split (path B's blocks go)", [safeThis] { if (safeThis != nullptr) safeThis->session.removeSplit(); });
    else if (! chain->hasSplit())
        menu.addItem ("Split into paths A and B here", [safeThis, index] { if (safeThis != nullptr) safeThis->session.addSplit (index); });

    // Inserting: in this block's lane (path B is lane 1).
    const auto lane = row == 2 ? 1 : 0;
    const auto inLane = row == 2 ? index - chain->getNumMainBlocks() : index;
    menu.addSeparator();
    menu.addSubMenu ("Insert before", blockTypeMenu (inLane, lane));
    menu.addSubMenu ("Insert after", blockTypeMenu (inLane + 1, lane));
    menu.addSeparator();
    menu.addItem ("Remove", [safeThis, index]
    {
        if (safeThis != nullptr) safeThis->session.removeBlock (index);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (tiles[(size_t) index])));
}

} // namespace encore
