// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/StageView.h"

namespace encore
{

const juce::Colour StageView::background = juce::Colour (0xff0b0d10);
const juce::Colour StageView::accent = juce::Colour (0xff4c9aff);

namespace
{
    const auto cardColour  = juce::Colour (0xff171b21);
    const auto onColour    = juce::Colour (0xff2e9e5b);
    const auto panicRed    = juce::Colour (0xffc62f35);
    const auto amber       = juce::Colour (0xfff0b429);
    const auto dim         = juce::Colours::white.withAlpha (0.45f);

    constexpr int stripHeight = 30;
    constexpr int gap = 4;                         // between widgets, px
    constexpr juce::uint32 unlockHoldMs = 1000;    // hold the lock this long to unlock
    constexpr int handleSize = 14;                 // resize grip, px

    void noFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
    }

    /** The biggest font (up to maxHeight) that fits the text on one line,
        so text is never squashed sideways. */
    juce::Font fittedFont (const juce::String& text, float width, float maxHeight, bool bold)
    {
        const juce::Font reference (juce::FontOptions (100.0f, bold ? juce::Font::bold : juce::Font::plain));
        const auto textWidth = juce::GlyphArrangement::getStringWidth (reference, text);
        const auto height = textWidth > 0.0f ? juce::jmin (maxHeight, 100.0f * width * 0.97f / textWidth) : maxHeight;
        return reference.withHeight (juce::jmax (8.0f, height));
    }

    juce::String cueTitle (const RunItem& cue)
    {
        return cue.number.isNotEmpty() ? cue.number + "  " + cue.name : cue.name;
    }

    juce::String patchNameFor (const ShowModel& show, const RunItem& cue)
    {
        if (cue.uniquePatch)
            return cue.uniquePatch->name;

        if (const auto* patch = show.findPatch (cue.patchId))
            return patch->name;

        return "(patch missing)";
    }
}

//==============================================================================
/** One widget. It copies what it shows (strings, states) in update() and
    repaints only when that changed, so a static screen costs next to nothing. */
class StageView::Widget final : public juce::Component
{
public:
    Widget (StageView& o, const StageWidget& s) : spec (s), owner (o)
    {
        noFocus (*this);

        if (spec.type == StageWidgetType::tuner)
        {
            tuner = std::make_unique<TunerDisplay>();
            tuner->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*tuner);
        }

    }

    StageWidget spec;

    void update (const StageSnapshot& snapshot)
    {
        const auto& show = owner.session.getShow();
        const auto cues = owner.session.getCues();
        const auto index = owner.session.getCurrentCue();
        const auto* cue = juce::isPositiveAndBelow (index, (int) cues.size()) ? cues[(size_t) index] : nullptr;
        const auto* chain = owner.engine.getChain();

        title = main = sub = {};
        warning = false;
        rows.clear();
        tiles.clear();

        switch (spec.type)
        {
            case StageWidgetType::currentCue:
                title = cue != nullptr && cue->number.isNotEmpty() ? "CUE " + cue->number : juce::String ("CUE");

                if (const auto path = owner.session.getSectionPath (index); path.isNotEmpty())
                    title << "   " << path.toUpperCase();
                main = cue != nullptr ? cue->name
                     : ! cues.empty() ? juce::String ("Ready")                                  // before the first cue
                     : (chain != nullptr ? chain->label : juce::String ("No cues"));
                break;

            case StageWidgetType::nextCue:
            {
                title = "NEXT";
                const auto* next = juce::isPositiveAndBelow (index + 1, (int) cues.size()) ? cues[(size_t) index + 1] : nullptr;
                main = next != nullptr ? cueTitle (*next) : juce::String ("End");
                sub = next != nullptr ? patchNameFor (show, *next) : juce::String();
                break;
            }

            case StageWidgetType::patchName:
                title = "PATCH";
                main = chain != nullptr ? chain->label : juce::String ("-");
                break;

            case StageWidgetType::cueNote:
                title = cue != nullptr && cue->barMark.isNotEmpty() ? cue->barMark : juce::String ("NOTE");
                main = cue != nullptr ? cue->note : juce::String();
                break;

            case StageWidgetType::setList:
            {
                // A window of the running order around the current cue (it
                // stays near the top), with act/scene headings; cut cues left out.
                std::vector<Row> all;

                for (auto& r : owner.session.getRunningOrderRows())
                {
                    if (r.item->kind == RunItem::Kind::section)
                        all.push_back ({ -1, r.item->name, {}, false });
                    else if (r.cueIndex >= 0)
                        all.push_back ({ r.cueIndex, cueTitle (*r.item), patchNameFor (show, *r.item), r.cueIndex == index });
                }

                int currentRow = 0;

                for (int i = 0; i < (int) all.size(); ++i)
                    if (all[(size_t) i].current)
                        currentRow = i;

                // Show the heading just above the current cue too.
                auto first = juce::jmax (0, currentRow - 1);

                if (first > 0 && all[(size_t) first - 1].index < 0)
                    --first;

                const auto visible = juce::jmax (1, (int) ((float) (getHeight() - 12) / rowHeight()));
                first = juce::jlimit (0, juce::jmax (0, (int) all.size() - visible), first);

                for (int i = first; i < juce::jmin ((int) all.size(), first + visible); ++i)
                    rows.push_back (all[(size_t) i]);
                break;
            }

            case StageWidgetType::stomps:
                if (chain != nullptr)
                {
                    for (int n = 1; n <= maxStomps; ++n)
                    {
                        Tile tile { n, {}, false, 0.0f };

                        for (auto& block : chain->getBlocks())
                        {
                            if (block->getStomp() == n)
                            {
                                tile.label << (tile.label.isEmpty() ? "" : " + ") << block->getName();
                                tile.on = tile.on || block->isEnabled();
                            }
                        }

                        if (tile.label.isNotEmpty())
                            tiles.push_back (tile);
                    }
                }
                break;

            case StageWidgetType::pedals:
                for (int n = 1; n <= maxExpressionPedals; ++n)
                {
                    const auto position = owner.engine.getMidiRouter().getPedalPosition (n);
                    const auto used = chain != nullptr && chain->usesExpression (n);

                    if (used || position >= 0.0f)
                        tiles.push_back ({ n, "EXP " + juce::String (n), used, juce::jmax (0.0f, position) });
                }
                break;

            case StageWidgetType::tuner:
                tuner->setReading (snapshot.tuner, owner.engine.getTunerMutes(), owner.engine.getTuner().getReferencePitch());
                tuner->setAlpha (owner.engine.isTunerOpen() ? 1.0f : 0.35f);
                break;

            case StageWidgetType::meters:
            {
                // Fast up, slow down (about 20 dB a second), like a hardware meter.
                const float peaks[] = { snapshot.inputPeak, snapshot.outputPeak };

                for (int m = 0; m < 2; ++m)
                {
                    const auto db = juce::Decibels::gainToDecibels (peaks[m], -60.0f);
                    meterDb[m] = db > meterDb[m] ? db : juce::jmax (-60.0f, meterDb[m] - 0.7f);
                    tiles.push_back ({ m, m == 0 ? "IN" : "OUT", peaks[m] > 0.89f, (meterDb[m] + 60.0f) / 60.0f });
                }
                break;
            }

            case StageWidgetType::clock:
                main = juce::Time::getCurrentTime().formatted ("%H:%M");
                break;

            case StageWidgetType::health:
                title = "SYSTEM";

                if (snapshot.audioProblem.isNotEmpty())
                {
                    main = "AUDIO LOST";
                    sub = "reconnecting...";
                    warning = true;
                    break;
                }

                main = "CPU " + juce::String (juce::roundToInt (snapshot.cpu * 100.0)) + " %";
                sub = owner.recentOverrun ? "DROPOUT!" : (snapshot.onBattery ? "ON BATTERY" : "OK");
                warning = owner.recentOverrun || snapshot.onBattery || snapshot.cpu > 0.8;
                break;

            case StageWidgetType::panicButton:
            {
                const auto state = owner.engine.getPanicState();
                main = "PANIC";
                warning = state != AudioEngine::PanicState::none;
                sub = state == AudioEngine::PanicState::silent && owner.engine.getPanicMode() == AudioEngine::PanicMode::latch
                          ? "silent - press to return"
                          : (warning ? "silencing..." : juce::String());
                break;
            }

            case StageWidgetType::lockButton:
                main = owner.locked ? "LOCKED" : "LOCK";
                sub = owner.locked ? owner.unlockProgressText() : juce::String ("click to lock");
                warning = owner.locked;
                break;

            case StageWidgetType::nextButton:      main = "NEXT  >"; break;
            case StageWidgetType::previousButton:  main = "<  BACK"; break;
            case StageWidgetType::text:            main = spec.text; break;
        }

        // Repaint only when something visible changed.
        juce::String key;
        key << title << "|" << main << "|" << sub << "|" << (int) warning << "|" << (int) owner.locked;

        for (auto& r : rows)   key << r.index << r.name << r.patch << (int) r.current;
        for (auto& t : tiles)  key << t.number << t.label << (int) t.on << juce::roundToInt (t.position * 100.0f);

        if (key != shownKey)
        {
            shownKey = key;
            repaint();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        if (tuner != nullptr)
            tuner->setBounds (getLocalBounds());

        shownKey = {};   // row counts depend on the size
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        auto fill = cardColour;

        if (spec.type == StageWidgetType::panicButton)
            fill = warning ? panicRed : panicRed.darker (0.6f);
        else if (spec.type == StageWidgetType::lockButton && warning)
            fill = amber.darker (0.7f);
        else if (spec.type == StageWidgetType::health && warning)
            fill = panicRed.darker (0.4f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);

        if (tuner != nullptr)
            return;

        auto area = getLocalBounds().reduced (10, 6).toFloat();
        const auto scale = spec.textScale;

        if (spec.type == StageWidgetType::setList)
        {
            const auto rowHeight = this->rowHeight();

            for (auto& row : rows)
            {
                auto r = area.removeFromTop (rowHeight);

                if (row.index < 0)   // act/scene heading
                {
                    g.setColour (amber.withAlpha (0.8f));
                    g.setFont (juce::FontOptions (juce::jmin (rowHeight * 0.5f, 30.0f * scale), juce::Font::bold));
                    g.drawText (row.name.toUpperCase(), r, juce::Justification::bottomLeft, true);
                    continue;
                }

                if (row.current)
                {
                    g.setColour (accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (r.expanded (4.0f, 0.0f), 4.0f);
                }

                g.setColour (row.current ? juce::Colours::white : juce::Colours::white.withAlpha (0.7f));
                g.setFont (juce::FontOptions (juce::jmin (rowHeight * 0.62f, 40.0f * scale), row.current ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (row.name, r.removeFromLeft (r.getWidth() * 0.62f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
                g.setColour (dim);
                g.drawFittedText (row.patch, r.toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
            }

            if (rows.empty())
            {
                g.setColour (dim);
                g.setFont (juce::FontOptions (18.0f * scale));
                g.drawText ("No cues yet", area, juce::Justification::centred);
            }
            return;
        }

        if (spec.type == StageWidgetType::stomps || spec.type == StageWidgetType::pedals || spec.type == StageWidgetType::meters)
        {
            paintTiles (g, area);
            return;
        }

        // Text widgets: small title on top, the main text as big as fits, a sub line.
        if (title.isNotEmpty())
        {
            const auto titleHeight = juce::jlimit (11.0f, 30.0f, area.getHeight() * 0.18f) * juce::jmin (scale, 1.5f);
            g.setColour (dim);
            g.setFont (juce::FontOptions (titleHeight, juce::Font::bold));
            g.drawText (title, area.removeFromTop (titleHeight * 1.2f), juce::Justification::centredLeft);
        }

        const auto centred = spec.type == StageWidgetType::clock || spec.type == StageWidgetType::panicButton
                          || spec.type == StageWidgetType::lockButton || spec.type == StageWidgetType::nextButton
                          || spec.type == StageWidgetType::previousButton;

        if (sub.isNotEmpty())
        {
            const auto subHeight = juce::jlimit (11.0f, 40.0f, area.getHeight() * 0.25f) * juce::jmin (scale, 1.5f);
            g.setColour (spec.type == StageWidgetType::health || spec.type == StageWidgetType::lockButton
                             ? juce::Colours::white : dim);
            g.setFont (juce::FontOptions (subHeight));
            g.drawText (sub, area.removeFromBottom (subHeight * 1.2f), centred ? juce::Justification::centred
                                                                                 : juce::Justification::centredLeft);
        }

        g.setColour (spec.type == StageWidgetType::cueNote ? amber : juce::Colours::white);
        const auto maxHeight = juce::jmax (12.0f, area.getHeight() * 0.72f * scale);
        const auto justification = centred ? juce::Justification::centred : juce::Justification::centredLeft;

        if (spec.type == StageWidgetType::cueNote || spec.type == StageWidgetType::text)
        {
            // Can wrap: notes are sentences.
            g.setFont (juce::FontOptions (juce::jmin (maxHeight, juce::jmax (14.0f, area.getHeight() * 0.4f * scale)), juce::Font::bold));
            g.drawFittedText (main, area.toNearestInt(), justification, 4, 0.9f);
        }
        else
        {
            g.setFont (fittedFont (main, area.getWidth(), maxHeight, true));
            g.drawText (main, area, justification, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (spec.type == StageWidgetType::lockButton && owner.locked)
            owner.beginUnlockPress();

        // Panic always works, even locked: it's the emergency stop.
        if (spec.type == StageWidgetType::panicButton && ! e.mods.isPopupMenu())
        {
            owner.engine.panic();
            return;
        }

        if (owner.locked || e.mods.isPopupMenu())
            return;

        const auto type = spec.type;

        if (type == StageWidgetType::nextButton)      owner.session.next();
        else if (type == StageWidgetType::previousButton)  owner.session.previous();
        else if (type == StageWidgetType::lockButton)      owner.setLocked (true);
        else if (type == StageWidgetType::tuner)           owner.engine.setTunerOpen (! owner.engine.isTunerOpen());
        else if (type == StageWidgetType::setList && ! rows.empty())
        {
            const auto row = (int) ((float) (e.y - 6) / rowHeight());

            if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].index >= 0)
                owner.session.goToCue (rows[(size_t) row].index);
        }
        else if (type == StageWidgetType::stomps)
        {
            for (auto& [area, number] : tileHits)
                if (area.contains (e.getPosition()))
                    if (auto* chain = owner.engine.getChain())
                        chain->toggleStomp (number);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (spec.type == StageWidgetType::lockButton)
            owner.endUnlockPress();
    }

private:
    struct Row  { int index; juce::String name, patch; bool current; };

    float rowHeight() const noexcept { return juce::jlimit (18.0f, 120.0f, 40.0f * spec.textScale); }
    struct Tile { int number; juce::String label; bool on; float position; };

    void paintTiles (juce::Graphics& g, juce::Rectangle<float> area)
    {
        tileHits.clear();

        if (tiles.empty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (16.0f * spec.textScale));
            g.drawFittedText (spec.type == StageWidgetType::stomps ? "No stomp switches in this patch"
                                                                    : "No expression pedals",
                              area.toNearestInt(), juce::Justification::centred, 2);
            return;
        }

        // A row of tiles if it's wide, a column if it's tall.
        const auto count = (int) tiles.size();
        const auto horizontal = area.getWidth() >= area.getHeight() || spec.type == StageWidgetType::meters;
        const auto step = (horizontal ? area.getWidth() : area.getHeight()) / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const auto& tile = tiles[(size_t) i];
            auto r = (horizontal ? juce::Rectangle<float> (area.getX() + step * (float) i, area.getY(), step, area.getHeight())
                                 : juce::Rectangle<float> (area.getX(), area.getY() + step * (float) i, area.getWidth(), step))
                         .reduced (3.0f);
            tileHits.push_back ({ r.toNearestInt(), tile.number });

            if (spec.type == StageWidgetType::stomps)
            {
                g.setColour (tile.on ? onColour : juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colours::white.withAlpha (tile.on ? 1.0f : 0.55f));
                const auto numberHeight = juce::jmin (r.getHeight() * 0.4f, r.getWidth() * 0.5f) * spec.textScale;
                g.setFont (juce::FontOptions (numberHeight, juce::Font::bold));
                g.drawText (juce::String (tile.number), r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::centred);
                g.setFont (juce::FontOptions (juce::jmax (11.0f, numberHeight * 0.4f)));
                g.drawFittedText (tile.label + (tile.on ? "\nON" : "\noff"), r.toNearestInt(), juce::Justification::centredTop, 2, 0.6f);
            }
            else if (spec.type == StageWidgetType::meters)
            {
                // Level: a bar from -60 dB, red near 0 dB.
                auto labelArea = horizontal ? r.removeFromBottom (juce::jmin (24.0f, r.getHeight() * 0.25f))
                                            : r.removeFromLeft (juce::jmin (44.0f, r.getWidth() * 0.3f));
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (tile.on ? panicRed : onColour);
                g.fillRoundedRectangle (horizontal ? r.withTrimmedTop (r.getHeight() * (1.0f - tile.position))
                                                   : r.withWidth (r.getWidth() * tile.position), 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 20.0f, juce::jmin (labelArea.getHeight(), labelArea.getWidth()) * 0.6f)));
                g.drawText (tile.label, labelArea, juce::Justification::centred);
            }
            else
            {
                // Pedal: a bar filling from the bottom (heel) to the top (toe).
                auto labelArea = r.removeFromBottom (juce::jmin (24.0f, r.getHeight() * 0.25f));
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (tile.on ? accent : accent.withAlpha (0.4f));
                g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * (1.0f - tile.position)), 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::FontOptions (juce::jmax (11.0f, labelArea.getHeight() * 0.7f)));
                g.drawText (tile.label, labelArea, juce::Justification::centred);
            }
        }
    }

    StageView& owner;
    juce::String title, main, sub, shownKey;
    bool warning = false;
    std::vector<Row> rows;
    std::vector<Tile> tiles;
    std::vector<std::pair<juce::Rectangle<int>, int>> tileHits;
    std::unique_ptr<TunerDisplay> tuner;
    float meterDb[2] = { -60.0f, -60.0f };
};

//==============================================================================
/** Edit layout mode: sits over the widgets, draws the grid, and moves and
    resizes them in whole cells. Changes are saved when the mouse is let go. */
class StageView::Editor final : public juce::Component
{
public:
    Editor() { noFocus (*this); }

    void paint (juce::Graphics& g) override
    {
        const auto grid = getLocalBounds();
        const auto cw = (float) grid.getWidth() / (float) stageGridColumns;
        const auto ch = (float) grid.getHeight() / (float) stageGridRows;

        g.setColour (juce::Colours::white.withAlpha (0.06f));

        for (int c = 1; c < stageGridColumns; ++c)
            g.drawVerticalLine (juce::roundToInt (cw * (float) c), 0.0f, (float) getHeight());

        for (int r = 1; r < stageGridRows; ++r)
            g.drawHorizontalLine (juce::roundToInt (ch * (float) r), 0.0f, (float) getWidth());

        for (int i = 0; i < (int) widgets.size(); ++i)
        {
            auto r = bounds (widgets[(size_t) i]).toFloat();
            const auto isSelected = i == selected;

            g.setColour (isSelected ? accent : juce::Colours::white.withAlpha (0.4f));
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, isSelected ? 3.0f : 1.0f);

            // Its name, so empty widgets (no note yet...) can be found.
            const juce::Font labelFont (juce::FontOptions (12.0f));
            const auto labelText = describe (widgets[(size_t) i].type);
            auto label = r.reduced (6.0f).removeFromTop (18.0f);
            label = label.withWidth (juce::jmin (label.getWidth(), juce::GlyphArrangement::getStringWidth (labelFont, labelText) + 10.0f));
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (label, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (labelFont);
            g.drawText (labelText, label.reduced (4.0f, 0.0f), juce::Justification::centredLeft);

            if (isSelected)
            {
                g.setColour (accent);
                g.fillRect (r.removeFromRight ((float) handleSize).removeFromBottom ((float) handleSize));
            }
        }

        if (widgets.empty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (20.0f));
            g.drawText ("Empty layout: \"+ Widget\" or double-click to add one", getLocalBounds(), juce::Justification::centred);
        }
    }

    void setWidgets (std::vector<StageWidget> w)
    {
        widgets = std::move (w);

        if (selected >= (int) widgets.size())
            selected = -1;

        repaint();
    }

    std::function<void (const std::vector<StageWidget>&)> onPreview, onCommit;
    std::function<void (juce::Point<int> cell)> onAddAt;

    void mouseDown (const juce::MouseEvent& e) override
    {
        selected = widgetAt (e.getPosition());
        mode = Mode::none;

        if (selected >= 0)
        {
            original = widgets[(size_t) selected];
            const auto r = bounds (original);
            const auto nearRight = e.x > r.getRight() - handleSize, nearBottom = e.y > r.getBottom() - handleSize;
            mode = nearRight && nearBottom ? Mode::resizeBoth
                 : nearRight               ? Mode::resizeWidth
                 : nearBottom              ? Mode::resizeHeight
                                           : Mode::move;

            if (e.mods.isPopupMenu())
            {
                mode = Mode::none;
                showWidgetMenu();
            }
        }

        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto i = widgetAt (e.getPosition());
        auto newCursor = juce::MouseCursor::NormalCursor;

        if (i >= 0)
        {
            const auto r = bounds (widgets[(size_t) i]);
            const auto nearRight = e.x > r.getRight() - handleSize, nearBottom = e.y > r.getBottom() - handleSize;
            newCursor = nearRight && nearBottom ? juce::MouseCursor::BottomRightCornerResizeCursor
                   : nearRight               ? juce::MouseCursor::LeftRightResizeCursor
                   : nearBottom              ? juce::MouseCursor::UpDownResizeCursor
                                             : juce::MouseCursor::DraggingHandCursor;
        }

        setMouseCursor (newCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selected < 0 || mode == Mode::none)
            return;

        const auto dx = juce::roundToInt ((float) e.getDistanceFromDragStartX() / cellWidth());
        const auto dy = juce::roundToInt ((float) e.getDistanceFromDragStartY() / cellHeight());
        auto w = original;

        if (mode == Mode::move)                                          { w.x += dx; w.y += dy; }
        if (mode == Mode::resizeWidth || mode == Mode::resizeBoth)       w.width += dx;
        if (mode == Mode::resizeHeight || mode == Mode::resizeBoth)      w.height += dy;

        // Resizing never pushes a widget off the grid: shrink it instead.
        if (mode != Mode::move)
        {
            w.width = juce::jlimit (1, stageGridColumns - w.x, w.width);
            w.height = juce::jlimit (1, stageGridRows - w.y, w.height);
        }

        w = clampToGrid (w);

        if (w != widgets[(size_t) selected])
        {
            widgets[(size_t) selected] = w;
            onPreview (widgets);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (mode != Mode::none && selected >= 0 && widgets[(size_t) selected] != original)
            onCommit (widgets);

        mode = Mode::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (widgetAt (e.getPosition()) < 0)
            onAddAt ({ (int) ((float) e.x / cellWidth()), (int) ((float) e.y / cellHeight()) });
    }

    int selected = -1;

private:
    enum class Mode { none, move, resizeWidth, resizeHeight, resizeBoth };

    float cellWidth() const  { return (float) getWidth() / (float) stageGridColumns; }
    float cellHeight() const { return (float) getHeight() / (float) stageGridRows; }

    juce::Rectangle<int> bounds (const StageWidget& w) const
    {
        return juce::Rectangle<float> ((float) w.x * cellWidth(), (float) w.y * cellHeight(),
                                       (float) w.width * cellWidth(), (float) w.height * cellHeight())
                   .toNearestInt().reduced (gap / 2);
    }

    int widgetAt (juce::Point<int> p) const
    {
        for (int i = (int) widgets.size(); --i >= 0;)   // topmost first
            if (bounds (widgets[(size_t) i]).contains (p))
                return i;

        return -1;
    }

    void showWidgetMenu()
    {
        const auto index = selected;
        juce::Component::SafePointer<Editor> safe (this);

        auto change = [safe, index] (std::function<void (std::vector<StageWidget>&)> edit)
        {
            if (safe == nullptr || ! juce::isPositiveAndBelow (index, (int) safe->widgets.size()))
                return;

            auto copy = safe->widgets;
            edit (copy);
            safe->onCommit (copy);
        };

        juce::PopupMenu size;
        const std::pair<const char*, float> sizes[] = { { "Small", 0.7f }, { "Normal", 1.0f }, { "Large", 1.4f },
                                                        { "Huge", 2.0f }, { "Biggest", 3.0f } };

        for (auto& [name, value] : sizes)
            size.addItem (name, true, std::abs (widgets[(size_t) index].textScale - value) < 0.01f,
                          [change, index, value = value] { change ([index, value] (auto& w) { w[(size_t) index].textScale = value; }); });

        juce::PopupMenu menu;
        menu.addSectionHeader (describe (widgets[(size_t) index].type));
        menu.addSubMenu ("Text size", size);

        if (widgets[(size_t) index].type == StageWidgetType::text)
            menu.addItem ("Edit text...", [safe, index] { if (safe != nullptr) safe->editText (index); });

        menu.addItem ("Duplicate", [change, index] { change ([index] (auto& w)
        {
            auto copy = w[(size_t) index];
            copy.x += 1;
            copy.y += 1;
            w.push_back (clampToGrid (copy));
        }); });
        menu.addItem ("Bring to front", [change, index] { change ([index] (auto& w)
        {
            auto item = w[(size_t) index];
            w.erase (w.begin() + index);
            w.push_back (item);
        }); });
        menu.addSeparator();
        menu.addItem ("Remove", [change, index] { change ([index] (auto& w) { w.erase (w.begin() + index); }); });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void editText (int index)
    {
        auto* window = new juce::AlertWindow ("Text", "What this widget says (several lines are fine).",
                                              juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("text", widgets[(size_t) index].text);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<Editor> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, index] (int result)
        {
            if (safe == nullptr || result != 1 || ! juce::isPositiveAndBelow (index, (int) safe->widgets.size()))
                return;

            auto copy = safe->widgets;
            copy[(size_t) index].text = window->getTextEditorContents ("text");
            safe->onCommit (copy);
        }), true);
    }

    std::vector<StageWidget> widgets;
    StageWidget original;
    Mode mode = Mode::none;
};

//==============================================================================
StageView::StageView (AudioEngine& e, ShowSession& s) : engine (e), session (s)
{
    noFocus (*this);

    for (auto* b : { &editViewButton, &editLayoutButton, &lockStripButton, &layoutsButton, &addWidgetButton, &doneButton })
    {
        noFocus (*b);
        addAndMakeVisible (b);
    }

    editViewButton.setTooltip ("Back to editing patches and the set list");
    editViewButton.onClick = [this] { if (onLeave != nullptr) onLeave(); };

    editLayoutButton.setTooltip ("Move, resize, add and remove what this screen shows");
    editLayoutButton.onClick = [this] { setEditingLayout (true); };

    lockStripButton.setTooltip ("Lock the screen so nothing changes by accident (footswitches keep working)");
    lockStripButton.onClick = [this] { if (! locked) setLocked (true); };
    lockStripButton.onStateChange = [this]
    {
        if (! locked)
            return;

        if (lockStripButton.isDown())
            beginUnlockPress();
        else
            endUnlockPress();
    };

    layoutsButton.onClick = [this] { showLayoutMenu(); };
    addWidgetButton.onClick = [this] { showAddWidgetMenu(); };
    doneButton.onClick = [this] { setEditingLayout (false); };

    hint.setText ("Drag to move, drag the edges to resize, right-click for options, double-click empty space to add",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, dim);
    hint.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (hint);

    noticeLabel.setColour (juce::Label::textColourId, amber);
    noticeLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    noticeLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (noticeLabel);

    noFocus (tunerOverlay);
    tunerOverlay.setOpaque (true);
    tunerOverlay.onClick = [this] { if (! locked) engine.setTunerOpen (false); };
    addChildComponent (tunerOverlay);

    setLocked (false);
    setEditingLayout (false);
}

StageView::~StageView() = default;

//==============================================================================
const StageLayout& StageView::shownLayout() const
{
    if (editing)
    {
        if (const auto* layout = session.getStage().findLayout (editingLayoutId))
            return *layout;
    }

    return session.getLayoutForCue (session.getCurrentCue());
}

bool StageView::layoutHasTuner() const
{
    return std::any_of (current.widgets.begin(), current.widgets.end(),
                        [] (const StageWidget& w) { return w.type == StageWidgetType::tuner; });
}

juce::Rectangle<int> StageView::getGridArea() const
{
    return getLocalBounds().withTrimmedTop (stripHeight).reduced (gap);
}

juce::Rectangle<int> StageView::cellBounds (const StageWidget& w) const
{
    const auto grid = getGridArea().toFloat();
    const auto cw = grid.getWidth() / (float) stageGridColumns, ch = grid.getHeight() / (float) stageGridRows;
    return juce::Rectangle<float> (grid.getX() + (float) w.x * cw, grid.getY() + (float) w.y * ch,
                                   (float) w.width * cw, (float) w.height * ch)
               .toNearestInt().reduced (gap / 2);
}

void StageView::rebuildWidgets()
{
    widgets.clear();

    for (auto& spec : current.widgets)
    {
        auto* widget = widgets.add (new Widget (*this, spec));
        widget->setInterceptsMouseClicks (! editing, ! editing);
        addAndMakeVisible (widget);
    }

    if (editor != nullptr)
    {
        editor->setWidgets (current.widgets);
        editor->toFront (false);
    }

    tunerOverlay.toFront (false);
    layoutWidgets();
    update (last);
}

void StageView::layoutWidgets()
{
    for (auto* widget : widgets)
        widget->setBounds (cellBounds (widget->spec));
}

void StageView::commitWidgets (std::vector<StageWidget> newWidgets)
{
    if (editingLayoutId.isEmpty())
        editingLayoutId = shownLayout().id;

    session.setLayoutWidgets (editingLayoutId, std::move (newWidgets));
    current = shownLayout();
    rebuildWidgets();
}

//==============================================================================
void StageView::update (const StageSnapshot& snapshot)
{
    last = snapshot;

    // Overruns in the last 10 s turn the health widget red.
    if (snapshot.overruns != lastOverruns)
    {
        lastOverruns = snapshot.overruns;
        lastOverrunTime = juce::Time::getMillisecondCounter();
    }

    recentOverrun = lastOverrunTime != 0 && juce::Time::getMillisecondCounter() - lastOverrunTime < 10000;

    // The layout follows the cue (unless it's being edited).
    if (const auto& layout = shownLayout(); ! (layout == current))
    {
        current = layout;
        rebuildWidgets();
        return;   // rebuildWidgets() updates them
    }

    if (locked && unlockPressStart != 0 && juce::Time::getMillisecondCounter() - unlockPressStart >= unlockHoldMs)
    {
        unlockPressStart = 0;
        setLocked (false);
    }

    for (auto* widget : widgets)
        widget->update (snapshot);

    // The tuner covers the screen when the layout has no tuner of its own.
    const auto overlay = engine.isTunerOpen() && ! layoutHasTuner() && ! editing;

    if (overlay != tunerOverlay.isVisible())
        tunerOverlay.setVisible (overlay);

    if (overlay)
        tunerOverlay.setReading (snapshot.tuner, engine.getTunerMutes(), engine.getTuner().getReferencePitch());

    if (locked)
        lockStripButton.setButtonText ("LOCKED: " + unlockProgressText());

    // Along the top: a lost interface first, then any notice.
    const auto text = snapshot.audioProblem.isNotEmpty() ? snapshot.audioProblem : snapshot.notice;

    if (text != noticeLabel.getText())
    {
        noticeLabel.setText (text, juce::dontSendNotification);
        noticeLabel.setColour (juce::Label::textColourId, snapshot.audioProblem.isNotEmpty() ? panicRed.brighter (0.3f) : amber);
    }
}

void StageView::setLocked (bool shouldLock)
{
    locked = shouldLock;
    unlockPressStart = 0;

    if (locked && editing)
        setEditingLayout (false);

    lockStripButton.setButtonText (locked ? "LOCKED: hold to unlock" : "Lock");
    lockStripButton.setColour (juce::TextButton::buttonColourId, locked ? amber.darker (0.6f) : cardColour);
    editViewButton.setVisible (! locked && ! editing);
    editLayoutButton.setVisible (! locked && ! editing);

    for (auto* widget : widgets)
        widget->update (last);

    resized();
}

void StageView::beginUnlockPress()  { if (locked) unlockPressStart = juce::jmax (1u, juce::Time::getMillisecondCounter()); }
void StageView::endUnlockPress()    { unlockPressStart = 0; }

juce::String StageView::unlockProgressText() const
{
    if (unlockPressStart == 0)
        return "hold to unlock";

    const auto held = juce::Time::getMillisecondCounter() - unlockPressStart;
    return "unlocking " + juce::String (juce::jmin (100, (int) (held * 100 / unlockHoldMs))) + " %";
}

void StageView::setEditingLayout (bool shouldEdit)
{
    if (shouldEdit && locked)
        return;

    editing = shouldEdit;

    if (editing)
    {
        editingLayoutId = session.getLayoutForCue (session.getCurrentCue()).id;
        editor = std::make_unique<Editor>();
        editor->onPreview = [this] (const std::vector<StageWidget>& preview)
        {
            for (int i = 0; i < widgets.size() && i < (int) preview.size(); ++i)
            {
                widgets[i]->spec = preview[(size_t) i];
                widgets[i]->setBounds (cellBounds (preview[(size_t) i]));
            }
        };
        editor->onCommit = [this] (const std::vector<StageWidget>& w)
        {
            // From a menu callback or mouse-up: rebuild after it returns.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StageView> (this), w]
            {
                if (safe != nullptr)
                    safe->commitWidgets (w);
            });
        };
        editor->onAddAt = [this] (juce::Point<int> cell) { addWidgetCell = cell; showAddWidgetMenu(); };
        addAndMakeVisible (*editor);
    }
    else
    {
        editor = nullptr;
        editingLayoutId = {};
    }

    for (auto* b : { &layoutsButton, &addWidgetButton, &doneButton })
        b->setVisible (editing);

    hint.setVisible (editing);
    noticeLabel.setVisible (! editing);
    editViewButton.setVisible (! locked && ! editing);
    editLayoutButton.setVisible (! locked && ! editing);
    lockStripButton.setVisible (! editing);

    current = shownLayout();
    rebuildWidgets();
    resized();
}

//==============================================================================
void StageView::showLayoutMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<StageView> safe (this);
    const auto& stage = session.getStage();
    const auto editingId = current.id;
    const auto cueIndex = session.getCurrentCue();
    const auto cues = session.getCues();
    const auto cueLayout = juce::isPositiveAndBelow (cueIndex, (int) cues.size()) ? cues[(size_t) cueIndex]->layoutId : juce::String();

    menu.addSectionHeader ("Edit layout");

    if (stage.layouts.empty())
        menu.addItem ("Default", true, true, {});

    for (auto& layout : stage.layouts)
    {
        auto name = layout.name;

        if (layout.id == stage.defaultLayoutId || (stage.defaultLayoutId.isEmpty() && &layout == &stage.layouts.front()))
            name << "  (default)";

        menu.addItem (name, true, layout.id == editingId, [safe, id = layout.id]
        {
            if (safe != nullptr) { safe->editingLayoutId = id; safe->current = safe->shownLayout(); safe->rebuildWidgets(); safe->resized(); }
        });
    }

    auto askName = [safe] (const juce::String& title, const juce::String& start, std::function<void (juce::String)> done)
    {
        if (safe == nullptr)
            return;

        auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
        window->addTextEditor ("name", start);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, done] (int result)
        {
            if (result == 1 && window->getTextEditorContents ("name").trim().isNotEmpty())
                done (window->getTextEditorContents ("name").trim());
        }), true);
    };

    menu.addSeparator();
    menu.addItem ("New layout (copy of this one)...", [safe, askName, editingId]
    {
        askName ("New layout", "Big monitor", [safe, editingId] (juce::String name)
        {
            if (safe == nullptr) return;
            safe->editingLayoutId = safe->session.addLayout (name, editingId);
            safe->current = safe->shownLayout();
            safe->rebuildWidgets();
        });
    });
    menu.addItem ("Rename...", [safe, askName, editingId, name = current.name]
    {
        askName ("Rename layout", name, [safe, editingId] (juce::String newName)
        {
            if (safe != nullptr) safe->session.renameLayout (editingId, newName);
        });
    });
    menu.addItem ("Use as the show's default", [safe, editingId] { if (safe != nullptr) safe->session.setDefaultLayout (editingId); });

    if (juce::isPositiveAndBelow (cueIndex, (int) cues.size()))
    {
        const auto cueName = cueTitle (*cues[(size_t) cueIndex]);
        menu.addItem ("Use for cue " + cueName + " only", true, cueLayout == editingId,
                      [safe, cueIndex, editingId] { if (safe != nullptr) safe->session.setCueLayout (cueIndex, editingId); });

        if (cueLayout.isNotEmpty())
            menu.addItem ("Cue " + cueName + ": back to the default layout",
                          [safe, cueIndex] { if (safe != nullptr) safe->session.setCueLayout (cueIndex, {}); });
    }

    menu.addSeparator();
    menu.addItem ("Delete this layout", stage.layouts.size() > 1, false, [safe, editingId]
    {
        if (safe != nullptr && safe->session.deleteLayout (editingId))
        {
            safe->editingLayoutId = {};
            safe->current = safe->shownLayout();
            safe->rebuildWidgets();
        }
    });
    menu.addItem ("Reset to the built-in layout", [safe]
    {
        if (safe != nullptr)
            safe->commitWidgets (makeDefaultStageLayout().widgets);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&layoutsButton));
}

void StageView::showAddWidgetMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<StageView> safe (this);
    const auto cell = addWidgetCell;
    addWidgetCell = {};

    for (auto type : allStageWidgetTypes())
    {
        menu.addItem (describe (type), [safe, type, cell]
        {
            if (safe == nullptr)
                return;

            StageWidget w;
            w.type = type;
            w.x = cell.x;
            w.y = cell.y;
            w.width = type == StageWidgetType::setList ? 8 : (type == StageWidgetType::tuner ? 10 : 6);
            w.height = type == StageWidgetType::setList ? 8 : (type == StageWidgetType::tuner ? 5 : 3);

            if (type == StageWidgetType::text)
                w.text = "Text";

            auto list = safe->current.widgets;
            list.push_back (clampToGrid (w));
            safe->commitWidgets (list);
        });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addWidgetButton));
}

//==============================================================================
void StageView::paint (juce::Graphics& g)
{
    g.fillAll (background);

    if (editing)
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRect (getLocalBounds().removeFromTop (stripHeight));
    }
}

void StageView::resized()
{
    auto strip = getLocalBounds().removeFromTop (stripHeight).reduced (gap, 3);

    if (editing)
    {
        layoutsButton.setButtonText ("Layout: " + current.name + "  v");
        layoutsButton.setBounds (strip.removeFromLeft (240));
        strip.removeFromLeft (gap);
        addWidgetButton.setBounds (strip.removeFromLeft (100));
        doneButton.setBounds (strip.removeFromRight (90));
        hint.setBounds (strip.reduced (8, 0));
    }
    else
    {
        noticeLabel.setBounds (strip.removeFromLeft (strip.getWidth() / 2));
        lockStripButton.setBounds (strip.removeFromRight (locked ? 220 : 70));
        strip.removeFromRight (gap);
        editLayoutButton.setBounds (strip.removeFromRight (100));
        strip.removeFromRight (gap);
        editViewButton.setBounds (strip.removeFromRight (90));
    }

    if (editor != nullptr)
        editor->setBounds (getGridArea());

    tunerOverlay.setBounds (getGridArea());
    layoutWidgets();
}

} // namespace encore
