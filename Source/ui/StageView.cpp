// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/StageView.h"

#include "ui/ChainStrip.h"
#include "ui/kit/GearPlaceholder.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    constexpr juce::uint32 unlockHoldMs = 1000;    // hold the lock this long to unlock
    constexpr int handleSize = 14;                 // layout editor: resize grip, px
    constexpr float warningCpu = 0.8f;             // the health widget warns above this load

    const juce::String& middot()
    {
        static const juce::String s (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        return s;
    }


    void noFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
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

    /** A widget's rectangle in the grid: cells (W + gap) / 24 wide, the gap
        between neighbours (SCREENS.md §3.3). */
    juce::Rectangle<int> gridCell (juce::Rectangle<int> grid, const StageWidget& w)
    {
        const auto gap = (float) size::gap;
        const auto cw = ((float) grid.getWidth() + gap) / (float) stageGridColumns;
        const auto ch = ((float) grid.getHeight() + gap) / (float) stageGridRows;
        const auto left = juce::roundToInt ((float) grid.getX() + (float) w.x * cw);
        const auto top = juce::roundToInt ((float) grid.getY() + (float) w.y * ch);
        const auto right = juce::roundToInt ((float) grid.getX() + (float) (w.x + w.width) * cw - gap);
        const auto bottom = juce::roundToInt ((float) grid.getY() + (float) (w.y + w.height) * ch - gap);
        return { left, top, juce::jmax (0, right - left), juce::jmax (0, bottom - top) };
    }

    float lineOf (const juce::Font& f)          { return f.getAscent() + f.getDescent(); }

    /** The baseline of a line of text in a box of `lineHeight` starting at
        `top` (as CSS places it). */
    float baselineIn (const juce::Font& f, float top, float lineHeight)
    {
        return top + (lineHeight - lineOf (f)) * 0.5f + f.getAscent();
    }

    /** Cuts text to fit `width` with an ellipsis (list rows only). */
    juce::String ellipsized (const juce::Font& f, const juce::String& text, float width)
    {
        if (width <= 0.0f)
            return {};

        if (textWidth (f, text) <= width)
            return text;

        const auto dots = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        auto cut = text;

        while (cut.isNotEmpty() && textWidth (f, cut.trimEnd() + dots) > width)
            cut = cut.dropLastCharacters (1);

        return cut.isEmpty() ? juce::String() : cut.trimEnd() + dots;
    }

    /** Words wrapped to `width` (explicit line breaks kept). */
    juce::StringArray wrapped (const juce::Font& f, const juce::String& text, float width)
    {
        juce::StringArray lines;

        for (const auto& paragraph : juce::StringArray::fromLines (text))
        {
            juce::String line;

            for (const auto& word : juce::StringArray::fromTokens (paragraph, " ", {}))
            {
                const auto candidate = line.isEmpty() ? word : line + " " + word;

                if (line.isNotEmpty() && textWidth (f, candidate) > width)
                {
                    lines.add (line);
                    line = word;
                }
                else
                {
                    line = candidate;
                }
            }

            lines.add (line);
        }

        return lines;
    }
}

//==============================================================================
/** A line of plain text in the toolbar ("Layout “Default”", the edit hint). */
class StageView::Caption final : public juce::Component
{
public:
    explicit Caption (const type::Style& s) : style (s) { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& newText)
    {
        if (newText != text)
        {
            text = newText;
            width = textWidth (font (style), text);
            repaint();
        }
    }

    int getIdealWidth() const noexcept { return (int) std::ceil (width) + 1; }

    void paint (juce::Graphics& g) override
    {
        paint::text (g, text, getLocalBounds().toFloat(), style, colour::textMuted);
    }

private:
    const type::Style& style;
    juce::String text;
    float width = 0.0f;
};

//==============================================================================
/** CPU with a 4 px bar and the dropout count, in the toolbar (§3.1). */
class StageView::MiniMeters final : public juce::Component
{
public:
    MiniMeters() { setInterceptsMouseClicks (false, false); }

    void setValues (int cpuPercent, int dropouts)
    {
        if (cpuPercent != cpu || dropouts != drops)
        {
            cpu = cpuPercent;
            drops = dropouts;
            cpuText = juce::String (cpu) + "%";
            dropsText = juce::String (drops);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto lineH = lineOf (font (type::miniMeterLabel));
        const auto blockH = lineH * 2.0f + (float) (size::miniMeterBarH + 2 * size::miniMeterGap);
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), blockH);

        auto top = r.removeFromTop (lineH);
        paint::text (g, cpuLabel, top, type::miniMeterLabel, colour::textMuted);
        paint::text (g, cpuText, top, type::miniMeterLabel, colour::textMuted, juce::Justification::centredRight);
        r.removeFromTop ((float) size::miniMeterGap);

        const auto bar = r.removeFromTop ((float) size::miniMeterBarH);
        const auto radius = (float) size::miniMeterBarH * 0.5f;
        g.setColour (paint::c (colour::meterTrack));
        g.fillRoundedRectangle (bar, radius);
        g.setColour (paint::c ((float) cpu > warningCpu * 100.0f ? colour::amber : colour::healthy));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0, 100, cpu) / 100.0f), radius);
        r.removeFromTop ((float) size::miniMeterGap);

        paint::text (g, dropsLabel, r, type::miniMeterLabel, colour::textMuted);
        paint::text (g, dropsText, r, type::miniMeterLabel, colour::textMuted, juce::Justification::centredRight);
    }

private:
    const juce::String cpuLabel { "CPU" }, dropsLabel { "DROPOUTS" };
    juce::String cpuText { "0%" }, dropsText { "0" };
    int cpu = 0, drops = 0;
};

//==============================================================================
/** One widget (SCREENS.md §3.3, DESIGN-SYSTEM.md §5.11). update() gathers
    what it shows; only when that changed does it lay the content out again
    (texts, fitted fonts, bars) and repaint, so a still screen costs nothing
    and paint() only draws what was laid out. */
class StageView::Widget final : public juce::Component
{
public:
    Widget (StageView& o, const StageWidget& s) : spec (s), owner (o)
    {
        noFocus (*this);

        if (spec.type == StageWidgetType::tuner)
        {
            tuner = std::make_unique<TunerDisplay>();
            tuner->setOverlay (false);
            tuner->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*tuner);
        }
    }

    StageWidget spec;

    void update (const StageSnapshot& snapshot)
    {
        Content next;
        gather (snapshot, next);

        if (spec.type == StageWidgetType::tuner)
        {
            tuner->setReading (snapshot.tuner, owner.engine.getTunerMutes(), owner.engine.getTuner().getReferencePitch());
            tuner->setAlpha (owner.engine.isTunerOpen() ? 1.0f : state::disabledAlpha);
        }

        // Lay out and repaint only when something visible changed.
        if (! (next == content))
        {
            content = std::move (next);
            layOut();
            repaint();
        }
    }

    void resized() override
    {
        // The set list's window depends on the size: gather again.
        Content next;
        gather (owner.last, next);
        content = std::move (next);
        layOut();
    }

    void paint (juce::Graphics& g) override
    {
        paintFrame (g);

        for (auto& bar : bars)
        {
            g.setColour (paint::c (colour::widgetTrack));
            g.fillPath (bar.track);

            if (! bar.fill.isEmpty())
            {
                const juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (bar.track);

                if (bar.solid != 0)
                    g.setColour (paint::c (bar.solid).withMultipliedAlpha (bar.alpha));
                else
                    g.setGradientFill (bar.gradient);

                g.fillRect (bar.fill);
            }
        }

        for (auto* pedal : pedals)
            kit::GearPlaceholder::paintPedalShadow (g, pedal->getBounds().toFloat());

        for (auto& row : rowFills)
        {
            g.setColour (paint::c (colour::selectedRow));
            g.fillRoundedRectangle (row, (float) size::rowRadius);
        }

        for (auto& mark : marks)
            drawIcon (g, mark.icon, mark.box, paint::c (mark.colour));

        for (auto& run : runs)
        {
            g.setColour (paint::c (run.colour));
            g.setFont (run.font);
            g.drawSingleLineText (run.text, juce::roundToInt (run.x), juce::roundToInt (run.baseline), run.justification);
        }

        paintTag (g);
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
        const auto at = e.position;

        if (type == StageWidgetType::nextButton)           owner.session.next();
        else if (type == StageWidgetType::previousButton)  owner.session.previous();
        else if (type == StageWidgetType::lockButton)      owner.setLocked (true);
        else if (type == StageWidgetType::tuner)           owner.engine.setTunerOpen (! owner.engine.isTunerOpen());
        else if (type == StageWidgetType::setList || type == StageWidgetType::stomps)
        {
            for (auto& [area, number] : hits)
            {
                if (! area.contains (at))
                    continue;

                if (type == StageWidgetType::setList)
                    owner.session.goToCue (number);
                else if (auto* chain = owner.engine.getChain())
                    chain->toggleStomp (number);

                break;
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (spec.type == StageWidgetType::lockButton)
            owner.endUnlockPress();
    }

private:
    enum class State { normal, warning, alert, active };

    struct Row
    {
        int index = -1;            // cue index; -1 for a heading
        juce::String number, name, patch;
        bool current = false, played = false;
        bool operator== (const Row&) const = default;
    };

    struct Tile
    {
        int number = 0;
        kit::BlockInfo info;       // stomps
        juce::String label;        // pedals, meters
        bool on = false;
        int permille = 0;          // pedal position, meter level
        bool operator== (const Tile&) const = default;
    };

    struct Content
    {
        juce::String title, main, sub;
        State state = State::normal;
        std::vector<Row> rows;
        std::vector<Tile> tiles;
        bool operator== (const Content&) const = default;
    };

    struct Run
    {
        juce::String text;
        juce::Font font { juce::FontOptions() };
        juce::uint32 colour = colour::text;
        float x = 0.0f, baseline = 0.0f;
        juce::Justification justification = juce::Justification::left;
    };

    struct Mark { Icon icon; juce::Rectangle<float> box; juce::uint32 colour; };

    struct Bar
    {
        juce::Path track;
        juce::Rectangle<float> fill;
        juce::ColourGradient gradient;
        juce::uint32 solid = 0;
        float alpha = 1.0f;
    };

    //==========================================================================
    /** What the widget shows now (strings and states, no layout). */
    void gather (const StageSnapshot& snapshot, Content& out) const
    {
        const auto& show = owner.session.getShow();
        const auto cues = owner.session.getCues();
        const auto index = owner.session.getCurrentCue();
        const auto* cue = juce::isPositiveAndBelow (index, (int) cues.size()) ? cues[(size_t) index] : nullptr;
        const auto* chain = owner.engine.getChain();

        switch (spec.type)
        {
            case StageWidgetType::currentCue:
            {
                out.title = owner.session.getWords().item.toUpperCase();

                if (cue != nullptr && cue->number.isNotEmpty())
                    out.title << " " << cue->number;

                if (const auto path = owner.session.getSectionPath (index); path.isNotEmpty())
                    out.title << "   " << path.toUpperCase();

                out.main = cue != nullptr ? cue->name
                         : ! cues.empty() ? juce::String ("Ready")                                  // before the first cue
                         : (chain != nullptr ? chain->label : juce::String ("No cues"));
                break;
            }

            case StageWidgetType::nextCue:
            {
                const auto* next = juce::isPositiveAndBelow (index + 1, (int) cues.size()) ? cues[(size_t) index + 1] : nullptr;
                // "4 Shuffle": one space, as the board shows it.
                out.main = next != nullptr ? (next->number.isNotEmpty() ? next->number + " " : juce::String()) + next->name
                                           : juce::String ("End");
                out.sub = next != nullptr ? patchNameFor (show, *next) : juce::String();
                break;
            }

            case StageWidgetType::patchName:
                out.main = chain != nullptr ? chain->label : juce::String ("-");
                break;

            case StageWidgetType::cueNote:
                out.main = cue != nullptr ? cue->note : juce::String();
                out.sub = cue != nullptr ? cue->barMark : juce::String();
                break;

            case StageWidgetType::setList:
                gatherRows (show, index, out.rows);
                break;

            case StageWidgetType::stomps:
                if (chain != nullptr)
                {
                    for (int n = 1; n <= maxStomps; ++n)
                    {
                        Tile tile;
                        tile.number = n;

                        for (auto& block : chain->getBlocks())
                        {
                            if (block->getStomp() != n)
                                continue;

                            if (tile.info.name.isEmpty())
                                tile.info = ChainStrip::describe (*block);
                            else
                                tile.info.name << " + " << block->getName();

                            tile.on = tile.on || block->isEnabled();
                        }

                        if (tile.info.name.isNotEmpty())
                        {
                            tile.info.shape = kit::BlockInfo::Shape::pedal;   // stomps are pedals on stage, amps too
                            tile.info.on = tile.on;
                            out.tiles.push_back (tile);
                        }
                    }
                }
                break;

            case StageWidgetType::pedals:
                for (int n = 1; n <= maxExpressionPedals; ++n)
                {
                    const auto position = owner.engine.getMidiRouter().getPedalPosition (n);
                    const auto used = chain != nullptr && chain->usesExpression (n);

                    if (used || position >= 0.0f)
                    {
                        const auto permille = juce::roundToInt (juce::jlimit (0.0f, 1.0f, position) * 1000.0f);
                        out.tiles.push_back ({ n, {}, "EXP " + juce::String (n) + middot() + juce::String ((permille + 5) / 10) + "%",
                                               used, permille });
                    }
                }
                break;

            case StageWidgetType::meters:
            {
                // Fast up, slow down (about 20 dB a second), like a hardware meter.
                const float peaks[] = { snapshot.inputPeak, snapshot.outputPeak };

                for (int m = 0; m < 2; ++m)
                {
                    const auto db = juce::Decibels::gainToDecibels (peaks[m], -60.0f);
                    meterDb[m] = db > meterDb[m] ? db : juce::jmax (-60.0f, meterDb[m] - 0.7f);
                    out.tiles.push_back ({ m, {}, m == 0 ? "IN" : "OUT", peaks[m] > 0.89f,
                                           juce::roundToInt ((meterDb[m] + 60.0f) / 60.0f * 1000.0f) });
                }
                break;
            }

            case StageWidgetType::clock:
                out.main = juce::Time::getCurrentTime().formatted ("%H:%M");
                break;

            case StageWidgetType::health:
            {
                if (snapshot.audioProblem.isNotEmpty())
                {
                    out.title = "System";
                    out.main = "AUDIO LOST";
                    out.sub = "reconnecting...";
                    out.state = State::alert;
                    break;
                }

                out.main = "CPU " + juce::String (juce::roundToInt (snapshot.cpu * 100.0)) + "%";
                out.sub = owner.recentOverrun ? "DROPOUT!" : (snapshot.onBattery ? "ON BATTERY" : "OK");
                out.sub << middot() << snapshot.overruns << (snapshot.overruns == 1 ? " dropout" : " dropouts");

                if (snapshot.roundTripMs > 0.0)
                    out.sub << middot() << juce::String (snapshot.roundTripMs, 1) << " ms";

                out.state = owner.recentOverrun || snapshot.onBattery || snapshot.cpu > warningCpu ? State::warning : State::normal;
                break;
            }

            case StageWidgetType::panicButton:
            {
                const auto panicState = owner.engine.getPanicState();
                out.main = "PANIC";
                out.state = panicState != AudioEngine::PanicState::none ? State::active : State::normal;
                out.sub = panicState == AudioEngine::PanicState::silent && owner.engine.getPanicMode() == AudioEngine::PanicMode::latch
                              ? juce::String ("silent - press to return")
                              : (out.state == State::active ? juce::String ("silencing...") : juce::String ("silence & reset all"));
                break;
            }

            case StageWidgetType::lockButton:
                out.main = owner.locked ? "LOCKED" : "LOCK";
                out.sub = owner.locked ? owner.unlockProgressText() : "click to lock" + middot() + "hold 1 s to unlock";
                out.state = owner.locked ? State::warning : State::normal;
                break;

            case StageWidgetType::nextButton:      out.main = "NEXT"; break;
            case StageWidgetType::previousButton:  out.main = "Back"; break;
            case StageWidgetType::text:            out.main = spec.text; break;
            case StageWidgetType::tuner:           break;
        }
    }

    /** A window of the running order around the current cue (it stays near
        the top), with act/scene headings; cut cues left out. As many rows as
        fit; if the end is reached first, earlier rows fill the space. */
    void gatherRows (const ShowModel& show, int index, std::vector<Row>& out) const
    {
        std::vector<Row> all;

        for (auto& r : owner.session.getRunningOrderRows())
        {
            if (r.item->kind == RunItem::Kind::section)
                all.push_back ({ -1, {}, r.item->name.toUpperCase(), {}, false, false });
            else if (r.cueIndex >= 0)
                all.push_back ({ r.cueIndex, r.item->number, r.item->name, patchNameFor (show, *r.item),
                                 r.cueIndex == index, r.cueIndex < index });
        }

        if (all.empty())
            return;

        int currentRow = 0;

        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].current)
                currentRow = i;

        // Start one row above the current one, with its heading if that's directly above.
        auto first = juce::jmax (0, currentRow - 1);

        if (first > 0 && all[(size_t) first - 1].index < 0)
            --first;

        const auto k = rowScale();
        const auto available = (float) getHeight() - listTop() - listBottom();
        const auto heightOf = [&] (const Row& r)
        {
            return (r.index < 0 ? (float) (size::stageHeadingRow + size::stageHeadingPadBottom) : (float) size::stageListRow) * k;
        };

        auto last = first;   // one past the last row shown
        auto used = 0.0f;

        for (; last < (int) all.size(); ++last)
        {
            const auto h = heightOf (all[(size_t) last]) + (last > first ? (float) size::stageListGap * k : 0.0f);

            if (used + h > available)
                break;

            used += h;
        }

        if (last == (int) all.size())
        {
            while (first > 0 && used + heightOf (all[(size_t) first - 1]) + (float) size::stageListGap * k <= available)
                used += heightOf (all[(size_t) --first]) + (float) size::stageListGap * k;
        }

        out.assign (all.begin() + first, all.begin() + juce::jmax (first, last));
    }

    //==========================================================================
    float scale() const noexcept     { return owner.getContentScale(); }
    float rowScale() const noexcept  { return owner.getContentScale() * spec.textScale; }
    float chromeScale() const noexcept { return juce::jmax (1.0f, owner.getContentScale()); }
    float listTop() const noexcept   { return 1.0f + (float) size::stageListTop * scale(); }
    float listBottom() const noexcept { return 1.0f + (float) size::stageListBottom * scale(); }
    float tagBottom() const noexcept { return tagBox.getBottom(); }

    /** A style's size at `k` times its reference size. */
    static float pxOf (const type::Style& style, float k)   { return style.px * k; }

    juce::Font fontFor (const type::Style& style, float px) const
    {
        return fontOfSize (style.weight, juce::jmax (1.0f, px), style.tracking);
    }

    /** The style at `k` times its size, shrunk so `text` fits `width`. */
    juce::Font fitted (const type::Style& style, const juce::String& text, float k, float width, float maxHeight = 1.0e6f) const
    {
        const auto byHeight = maxHeight / type::normalLine;
        const auto px = fittedSize (style.weight, text, width, juce::jmin (pxOf (style, k), byHeight), 1.0f, style.tracking);
        return fontFor (style, px);
    }

    void addRun (const juce::String& text, const juce::Font& f, juce::uint32 colourToken, float x, float baseline,
                 juce::Justification j = juce::Justification::left)
    {
        if (text.isNotEmpty())
            runs.push_back ({ text, f, colourToken, x, baseline, j });
    }

    //==========================================================================
    void layOut()
    {
        runs.clear();
        marks.clear();
        bars.clear();
        hits.clear();
        rowFills.clear();

        const auto W = (float) getWidth(), H = (float) getHeight();
        const auto s = scale(), k = rowScale();
        const auto b = 1.0f;   // the border

        layOutTag();

        if (W <= 2.0f || H <= 2.0f)
            return;

        switch (spec.type)
        {
            case StageWidgetType::currentCue:
            {
                const auto titleFont = fontFor (type::widgetTitle, pxOf (type::widgetTitle, s));
                const auto titleTop = juce::jmax (b + (float) size::widgetTitleY * s, tagBottom());   // never under the tag
                addRun (content.title, titleFont, colour::widgetTitle, b + (float) size::widgetTitleX * s,
                        baselineIn (titleFont, titleTop, lineOf (titleFont)));

                // The name: as big as fits, line height 1, its box ending 22 above the bottom.
                const auto left = b + (float) size::currentNameX * s;
                const auto bottom = H - b - (float) size::currentNameBottom * s;
                const auto room = bottom - (titleTop + lineOf (titleFont));
                const auto px = fittedSize (type::currentName.weight, content.main, W - 2.0f * left,
                                            juce::jmin ((float) size::stageCurrentBase * k, room), 1.0f, type::currentName.tracking);
                const auto nameFont = fontFor (type::currentName, px);
                addRun (ellipsized (nameFont, content.main, W - 2.0f * left), nameFont, colour::text, left, baselineIn (nameFont, bottom - px, px));
                break;
            }

            case StageWidgetType::nextCue:
            {
                const auto left = b + (float) size::widgetTextX * s, width = W - 2.0f * left;
                const auto bottom = H - b - (float) size::widgetTextBottom * s;
                const auto gapX = content.sub.isNotEmpty() ? (float) size::nextCueGap * s : 0.0f;
                auto mainFont = fontFor (type::nextCueName, pxOf (type::nextCueName, k));
                auto subFont = fontFor (type::nextCuePatch, pxOf (type::nextCuePatch, k));

                // Both shrink together until the line fits (width and height).
                const auto total = textWidth (mainFont, content.main) + gapX + textWidth (subFont, content.sub);
                const auto f = juce::jmin (1.0f, width / juce::jmax (1.0f, total), (bottom - tagBottom()) / juce::jmax (1.0f, lineOf (mainFont)));

                if (f < 1.0f)
                {
                    mainFont = fontFor (type::nextCueName, pxOf (type::nextCueName, k) * f);
                    subFont = fontFor (type::nextCuePatch, pxOf (type::nextCuePatch, k) * f);
                }

                const auto baseline = bottom - mainFont.getDescent();
                addRun (ellipsized (mainFont, content.main, width), mainFont, colour::text, left, baseline);
                const auto subX = left + textWidth (mainFont, content.main) + gapX * juce::jmin (1.0f, f);
                addRun (ellipsized (subFont, content.sub, left + width - subX), subFont, colour::textTertiary, subX, baseline);
                break;
            }

            case StageWidgetType::patchName:
            {
                const auto left = b + (float) size::widgetTextX * s;
                const auto bottom = H - b - (float) size::widgetTextBottom * s;
                const auto f = fitted (type::patchName, content.main, k, W - 2.0f * left, bottom - tagBottom());
                addRun (ellipsized (f, content.main, W - 2.0f * left), f, colour::text, left, bottom - f.getDescent());
                break;
            }

            case StageWidgetType::cueNote:   layOutNote (W, H); break;

            case StageWidgetType::clock:
            {
                const auto bottom = H - b - (float) size::clockBottom * s;
                const auto f = fitted (type::clock, content.main, k, W - 2.0f * b, bottom - tagBottom());
                addRun (content.main, f, colour::text, W * 0.5f, bottom - f.getDescent(), juce::Justification::horizontallyCentred);
                break;
            }

            case StageWidgetType::health:
            {
                const auto left = b + (float) size::healthX * s, width = W - left - b;
                const auto bottom = H - b - (float) size::healthBottom * s;
                const auto subFont = fitted (type::healthSub, content.sub, k, width);
                const auto mainFont = fitted (type::healthMain, content.main, k, width, bottom - lineOf (subFont) - tagBottom());
                const auto alert = content.state == State::alert;
                addRun (content.sub, subFont, alert ? colour::lostWidgetSub : content.state == State::warning ? colour::amber : colour::healthy,
                        left, bottom - subFont.getDescent());
                addRun (content.main, mainFont, alert ? colour::textStrong : colour::text,
                        left, bottom - lineOf (subFont) - mainFont.getDescent());
                break;
            }

            case StageWidgetType::lockButton:
            {
                // Icon, label and hint, centred below the tag.
                const auto margin = (float) size::widgetTextX * s;
                const auto hintFont = fitted (type::lockHint, content.sub, k, W - 2.0f * margin);
                const auto labelFont = fitted (type::lockLabel, content.main, k, W - 2.0f * margin);
                const auto gapY = (float) size::lockGap * k;
                auto icon = (float) size::lockIcon * k;
                const auto top = b + (float) size::lockPadTop * s;
                const auto room = H - b - top;
                auto blockH = icon + gapY + lineOf (labelFont) + gapY + lineOf (hintFont);

                if (blockH > room)   // small widget: the icon gives way first
                {
                    icon = juce::jmax (0.0f, icon - (blockH - room));
                    blockH = icon + gapY + lineOf (labelFont) + gapY + lineOf (hintFont);
                }

                auto y = top + (room - blockH) * 0.5f;
                const auto ink = owner.locked ? colour::amber : colour::text;

                if (icon > 0.0f)
                    marks.push_back ({ Icon::lock, { W * 0.5f - icon * 0.5f, y, icon, icon }, ink });

                y += icon + gapY;
                addRun (content.main, labelFont, ink, W * 0.5f, baselineIn (labelFont, y, lineOf (labelFont)), juce::Justification::horizontallyCentred);
                y += lineOf (labelFont) + gapY;
                addRun (content.sub, hintFont, colour::textTertiary, W * 0.5f, baselineIn (hintFont, y, lineOf (hintFont)),
                        juce::Justification::horizontallyCentred);
                break;
            }

            case StageWidgetType::panicButton:
            {
                const auto margin = (float) size::widgetTextX * s;
                const auto top = b + (float) size::panicPadTop * s;
                const auto room = H - b - top;
                const auto subFont = fitted (type::panicHint, content.sub, k, W - 2.0f * margin);
                const auto mainFont = fitted (type::panicLabel, content.main, k, W - 2.0f * margin, room - lineOf (subFont));
                const auto y = top + (room - lineOf (mainFont) - lineOf (subFont)) * 0.5f;
                addRun (content.main, mainFont, colour::textStrong, W * 0.5f, baselineIn (mainFont, y, lineOf (mainFont)),
                        juce::Justification::horizontallyCentred);
                addRun (content.sub, subFont, colour::panicSub, W * 0.5f,
                        baselineIn (subFont, y + lineOf (mainFont), lineOf (subFont)), juce::Justification::horizontallyCentred);
                break;
            }

            case StageWidgetType::previousButton:
            case StageWidgetType::nextButton:
            {
                // "◂ Back" / "NEXT ▸": text and a triangle centred together.
                const auto next = spec.type == StageWidgetType::nextButton;
                const auto& style = next ? type::nextButton : type::backButton;
                const auto margin = (float) size::widgetTextX * s;
                const auto top = b + (float) size::stageButtonPadTop * s;
                const auto room = H - b - top;
                auto f = fontFor (style, pxOf (style, k));
                auto triangle = f.getAscent() * size::stageTriangle;
                const auto gapX = (float) size::stageTriangleGap * k;
                const auto group = textWidth (f, content.main) + gapX + triangle;
                const auto shrink = juce::jmin (1.0f, (W - 2.0f * margin) / juce::jmax (1.0f, group), room / juce::jmax (1.0f, lineOf (f)));

                if (shrink < 1.0f)
                {
                    f = fontFor (style, pxOf (style, k) * shrink);
                    triangle *= shrink;
                }

                const auto textW = textWidth (f, content.main);
                const auto groupW = textW + gapX * shrink + triangle;
                const auto x = (W - groupW) * 0.5f;
                const auto baseline = baselineIn (f, top + (room - lineOf (f)) * 0.5f, lineOf (f));
                const auto ink = next ? colour::textStrong : colour::textSoft;
                const auto capCentre = baseline - f.getAscent() * size::stageCapCentre;
                const auto textX = next ? x : x + triangle + gapX * shrink;
                const auto triangleX = next ? x + textW + gapX * shrink : x;
                addRun (content.main, f, ink, textX, baseline);
                marks.push_back ({ next ? Icon::triangleRight : Icon::triangleLeft,
                                   { triangleX, capCentre - triangle * 0.5f, triangle, triangle }, ink });
                break;
            }

            case StageWidgetType::text:
            {
                const auto margin = (float) size::widgetTextX * s;
                const auto lines = juce::StringArray::fromLines (content.main);
                auto px = pxOf (type::stageText, k);

                for (auto& line : lines)
                    px = fittedSize (type::stageText.weight, line, W - 2.0f * margin, px, 1.0f);

                const auto room = H - b - tagBottom() - margin;
                px = juce::jmin (px, room / ((float) lines.size() * type::normalLine));
                const auto f = fontFor (type::stageText, px);
                auto y = tagBottom() + (room - lineOf (f) * (float) lines.size()) * 0.5f;

                for (auto& line : lines)
                {
                    addRun (line, f, colour::text, W * 0.5f, baselineIn (f, y, lineOf (f)), juce::Justification::horizontallyCentred);
                    y += lineOf (f);
                }
                break;
            }

            case StageWidgetType::setList:  layOutList (W, H); break;
            case StageWidgetType::stomps:   layOutStomps (W, H); break;
            case StageWidgetType::pedals:   layOutPedals (W, H); break;
            case StageWidgetType::meters:   layOutMeters (W, H); break;

            case StageWidgetType::tuner:
            {
                const auto margin = (float) size::widgetTextX * s;
                tuner->setBounds (juce::Rectangle<float> (margin, tagBottom() + margin * 0.5f, W - 2.0f * margin,
                                                          H - tagBottom() - margin * 1.5f).getSmallestIntegerContainer());
                break;
            }
        }
    }

    void layOutTag()
    {
        // The widget type's name at (6, 6) inside the border; two lines in a narrow widget.
        const auto c = chromeScale();
        const auto f = fontFor (type::widgetTag, type::widgetTag.px * c);
        const auto inset = 1.0f + (float) size::widgetTagInset * c;
        const auto padX = (float) size::widgetTagPadX * c;
        const auto maxW = juce::jmax (0.0f, (float) getWidth() - inset - 1.0f);
        const auto name = content.title.isNotEmpty() && spec.type == StageWidgetType::health ? content.title : describe (spec.type);

        tagLines = { name };
        auto w = textWidth (f, name) + 2.0f * padX;

        if (w > maxW)
        {
            tagLines = wrapped (f, name, maxW - 2.0f * padX);

            while (tagLines.size() > 2)
                tagLines.remove (tagLines.size() - 1);

            w = maxW;
        }

        const auto lineH = lineOf (f);
        tagBox = { inset, inset, w, juce::jmax ((float) size::widgetTagH * c, lineH * (float) tagLines.size() + 2.0f * c) };
        tagFont = f;
    }

    void layOutNote (float W, float H)
    {
        // The note (wrapped if long), the bar mark under it; from the bottom up.
        const auto s = scale(), k = rowScale(), b = 1.0f;
        const auto left = b + (float) size::widgetTextX * s, width = W - 2.0f * left;
        auto bottom = H - b - (float) size::cueNoteBottom * s;
        const auto barFont = fitted (type::cueBar, content.sub, k, width);

        if (content.sub.isNotEmpty())
        {
            addRun (content.sub, barFont, colour::textTertiary, left, bottom - barFont.getDescent());
            bottom -= lineOf (barFont) + (float) size::cueNoteGap * k;
        }

        if (content.main.isEmpty())
            return;

        // Shrink until the wrapped note fits the height above.
        const auto room = bottom - tagBottom();
        auto px = pxOf (type::cueNote, k);
        juce::StringArray lines;

        for (;;)
        {
            const auto f = fontFor (type::cueNote, px);
            lines = wrapped (f, content.main, width);

            if (lineOf (f) * (float) lines.size() <= room || px <= type::minTextPx)
                break;

            px *= type::shrinkStep;
        }

        const auto f = fontFor (type::cueNote, px);

        for (int i = lines.size(); --i >= 0;)
        {
            addRun (ellipsized (f, lines[i], width), f, colour::text, left, bottom - f.getDescent());
            bottom -= lineOf (f);

            if (bottom - lineOf (f) < tagBottom() - lineOf (f) * 0.5f)
                break;   // no more room: the first lines give way
        }
    }

    void layOutList (float W, float H)
    {
        const auto s = scale(), k = rowScale(), b = 1.0f;
        const auto x0 = b + (float) size::stageListX * s, rowW = W - 2.0f * x0;
        auto y = listTop();

        if (content.rows.empty())
        {
            const auto f = fitted (type::stageEmpty, "No cues yet", k, rowW);
            addRun ("No cues yet", f, colour::textMuted, W * 0.5f, baselineIn (f, (H - lineOf (f)) * 0.5f, lineOf (f)),
                    juce::Justification::horizontallyCentred);
            return;
        }

        const auto padX = (float) size::stageListPadX * k;
        const auto numberFont = fontFor (type::stageListNumber, pxOf (type::stageListNumber, k));
        const auto nameFont = fontFor (type::stageListName, pxOf (type::stageListName, k));
        const auto patchFont = fontFor (type::stageListPatch, pxOf (type::stageListPatch, k));
        const auto headingFont = fontFor (type::stageListHeading, pxOf (type::stageListHeading, k));

        for (auto& row : content.rows)
        {
            if (row.index < 0)
            {
                // Heading: bottom-aligned in 30, 3 under it.
                const auto h = (float) size::stageHeadingRow * k;
                addRun (ellipsized (headingFont, row.name, rowW - 2.0f * padX), headingFont, colour::amberSection, x0 + padX,
                        y + h - headingFont.getDescent());
                y += h + (float) size::stageHeadingPadBottom * k + (float) size::stageListGap * k;
                continue;
            }

            const auto h = (float) size::stageListRow * k;
            const juce::Rectangle<float> r (x0, y, rowW, h);
            hits.push_back ({ r, row.index });

            if (row.current)
                rowFills.push_back (r);

            auto inner = r.reduced (padX, 0.0f);
            const auto numberX = inner.getX();
            inner.removeFromLeft ((float) (size::stageListNumberW + size::stageListCellGap) * k);
            const auto patchW = juce::jmin (textWidth (patchFont, row.patch), inner.getWidth() * 0.45f);
            const auto nameW = inner.getWidth() - patchW - (patchW > 0.0f ? (float) size::stageListCellGap * k : 0.0f);

            addRun (row.number, numberFont, row.current ? colour::textStrong : colour::stageNumberDim, numberX,
                    baselineIn (numberFont, y, h));
            addRun (ellipsized (nameFont, row.name, nameW), nameFont, row.played ? colour::textDim : colour::text, inner.getX(),
                    baselineIn (nameFont, y, h));
            addRun (ellipsized (patchFont, row.patch, patchW), patchFont, row.current ? colour::selectedRowSub : colour::textMuted,
                    inner.getRight(), baselineIn (patchFont, y, h), juce::Justification::right);
            y += h + (float) size::stageListGap * k;
        }
    }

    void layOutStomps (float W, float H)
    {
        const auto s = scale(), k = rowScale(), b = 1.0f;
        const auto count = (int) content.tiles.size();

        // The pedals: as many as there are stomps, made only when that changes.
        while (pedals.size() > count)
            pedals.removeLast();

        while (pedals.size() < count)
            addAndMakeVisible (pedals.add (new kit::GearPlaceholder (kit::GearView::Size::stagePedal)));

        if (count == 0)
        {
            const auto f = fitted (type::stageEmpty, "No stomp switches in this patch", k, W - 2.0f * (float) size::stompsX * s);
            addRun ("No stomp switches in this patch", f, colour::textMuted, W * 0.5f,
                    baselineIn (f, (H - lineOf (f)) * 0.5f, lineOf (f)), juce::Justification::horizontallyCentred);
            return;
        }

        // 0.7 of a 160 x 210 pedal at the reference size, 22 apart, bottom-aligned;
        // smaller when they don't fit.
        const auto left = b + (float) size::stompsX * s, right = W - b - (float) size::stompsRight * s;
        const auto bottom = H - b - (float) size::stompsBottom * s;
        const auto gapX = (float) size::stompsGap * s;
        auto p = size::stagePedalScale * k;
        p = juce::jmin (p, (bottom - tagBottom() - gapX * 0.25f) / (float) size::pedalH);
        p = juce::jmin (p, (right - left - gapX * (float) (count - 1)) / ((float) size::pedalW * (float) count));
        p = juce::jmax (0.05f, p);
        const auto w = (float) size::pedalW * p, h = (float) size::pedalH * p;

        for (int i = 0; i < count; ++i)
        {
            const juce::Rectangle<float> r (left + (float) i * (w + gapX), bottom - h, w, h);
            pedals[i]->setBlock (content.tiles[(size_t) i].info);
            pedals[i]->setTitle ("Stomp " + juce::String (content.tiles[(size_t) i].number) + middot() + content.tiles[(size_t) i].info.name);
            pedals[i]->setBounds (r.toNearestInt());
            hits.push_back ({ r, content.tiles[(size_t) i].number });
        }
    }

    void layOutPedals (float W, float H)
    {
        const auto s = scale(), k = rowScale(), b = 1.0f;

        if (content.tiles.empty())
        {
            const auto lines = wrapped (fontFor (type::stageEmpty, pxOf (type::stageEmpty, k)), "No expression pedals", W - 2.0f * b - 2.0f * (float) size::stageEmptyPadX * s);
            const auto f = fontFor (type::stageEmpty, pxOf (type::stageEmpty, k));
            auto y = (H - lineOf (f) * (float) lines.size()) * 0.5f;

            for (auto& line : lines)
            {
                addRun (line, fitted (type::stageEmpty, line, k, W - 2.0f * b), colour::textMuted, W * 0.5f,
                        baselineIn (f, y, lineOf (f)), juce::Justification::horizontallyCentred);
                y += lineOf (f);
            }
            return;
        }

        // One bar per pedal, each in its own column: track 40 % of the column, label under it.
        const auto columnW = (W - 2.0f * b) / (float) content.tiles.size();
        const auto top = b + (float) size::barsTop * s, bottom = H - b - (float) size::barsBottom * s;
        const auto labelBottom = H - b - (float) size::barsLabelBottom * s;

        for (size_t i = 0; i < content.tiles.size(); ++i)
        {
            const auto& tile = content.tiles[i];
            const auto columnX = b + columnW * (float) i;
            const auto inset = columnW * size::pedalTrackInset;
            const juce::Rectangle<float> track (columnX + inset, top, juce::jmax (0.0f, columnW - 2.0f * inset), juce::jmax (0.0f, bottom - top));

            Bar bar;
            bar.track.addRoundedRectangle (track, (float) size::pedalTrackRadius);
            bar.fill = track.withTrimmedTop (track.getHeight() * (1.0f - (float) tile.permille / 1000.0f));
            bar.gradient = juce::ColourGradient::vertical (paint::c (colour::expressionTop).withMultipliedAlpha (tile.on ? 1.0f : state::disabledAlpha),
                                                           bar.fill.getY(),
                                                           paint::c (colour::expressionBot).withMultipliedAlpha (tile.on ? 1.0f : state::disabledAlpha),
                                                           bar.fill.getBottom());
            bars.push_back (std::move (bar));

            const auto f = fitted (type::barLabel, tile.label, k, columnW - 2.0f * b);
            addRun (tile.label, f, colour::textTertiary, columnX + columnW * 0.5f, labelBottom - f.getDescent(),
                    juce::Justification::horizontallyCentred);
        }
    }

    void layOutMeters (float W, float H)
    {
        const auto s = scale(), k = rowScale(), b = 1.0f;
        const auto inner = W - 2.0f * b;
        const auto left = b + inner * size::meterBarsInset, width = inner * (1.0f - 2.0f * size::meterBarsInset);
        const auto gapX = (float) size::meterBarsGap * s;
        const auto barW = juce::jmax (0.0f, (width - gapX) * 0.5f);
        const auto top = b + (float) size::barsTop * s, bottom = H - b - (float) size::barsBottom * s;
        const auto labelBottom = H - b - (float) size::barsLabelBottom * s;

        for (size_t i = 0; i < content.tiles.size() && i < 2; ++i)
        {
            const auto& tile = content.tiles[i];
            const juce::Rectangle<float> track (left + (float) i * (barW + gapX), top, barW, juce::jmax (0.0f, bottom - top));
            const auto level = (float) tile.permille / 1000.0f;

            Bar bar;
            bar.track.addRoundedRectangle (track, (float) size::meterBarRadius);
            bar.fill = track.withTrimmedTop (track.getHeight() * (1.0f - level));

            if (tile.on)   // clipping
            {
                bar.solid = colour::stageMeterClip;
            }
            else
            {
                // Green to half way up the track, then towards yellow at the top of the level.
                bar.gradient = juce::ColourGradient::vertical (paint::c (colour::stageMeterLow), track.getBottom(),
                                                               paint::c (level > 0.5f ? colour::stageMeterHigh : colour::stageMeterLow),
                                                               juce::jmin (bar.fill.getY(), track.getBottom() - 1.0f));

                if (level > 0.5f)
                    bar.gradient.addColour (0.5f / level, paint::c (colour::stageMeterLow));
            }

            bars.push_back (std::move (bar));

            // Labels spread as the board's "space-around": at a quarter and three quarters.
            const auto centre = b + inner * (i == 0 ? 0.25f : 0.75f);
            const auto f = fitted (type::barLabel, tile.label, k, inner * 0.5f);
            addRun (tile.label, f, colour::textTertiary, centre, labelBottom - f.getDescent(), juce::Justification::horizontallyCentred);
        }
    }

    //==========================================================================
    void paintFrame (juce::Graphics& g) const
    {
        const auto r = getLocalBounds().toFloat();
        const auto radius = (float) size::widgetRadius;
        const auto type = spec.type;
        const auto alert = type == StageWidgetType::panicButton || (type == StageWidgetType::health && content.state == State::alert);
        auto border = colour::widgetBorder;

        if (alert || type == StageWidgetType::nextButton)
        {
            paint::verticalGradient (g, r, alert ? colour::panicTop : colour::primaryTop, alert ? colour::panicBot : colour::primaryBot, radius);

            if (content.state == State::active)   // panic while it silences
            {
                g.setColour (juce::Colours::white.withAlpha (state::activeLighten));
                g.fillRoundedRectangle (r, radius);
            }

            border = alert ? colour::panicBorder : colour::primaryBorder;
        }
        else if (type == StageWidgetType::stomps)
        {
            // The pedal board: chain-board fill with an inset shadow along the top.
            g.setColour (paint::c (colour::chainBoard));
            g.fillRoundedRectangle (r, radius);
            const juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.reduced (1.0f).toNearestInt());
            g.setGradientFill (juce::ColourGradient::vertical (paint::c (colour::stageBoardShadow), r.getY() + 1.0f,
                                                               paint::c (colour::stageBoardShadow).withAlpha (0.0f),
                                                               r.getY() + 1.0f + (float) size::stageShadowH));
            g.fillRect (r.withHeight ((float) size::stageShadowH + 1.0f));
        }
        else
        {
            g.setColour (paint::c (colour::widget));
            g.fillRoundedRectangle (r, radius);
        }

        g.setColour (paint::c (border));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    void paintTag (juce::Graphics& g) const
    {
        if (tagBox.isEmpty())
            return;

        g.setColour (paint::c (colour::widgetTagFill));
        g.fillRoundedRectangle (tagBox, (float) size::widgetTagRadius);
        g.setColour (paint::c (colour::widgetTagText));
        g.setFont (tagFont);
        const auto padX = (float) size::widgetTagPadX * chromeScale();
        const auto lineH = lineOf (tagFont);
        auto y = tagBox.getY() + (tagBox.getHeight() - lineH * (float) tagLines.size()) * 0.5f;

        for (auto& line : tagLines)
        {
            g.drawSingleLineText (line, juce::roundToInt (tagBox.getX() + padX), juce::roundToInt (y + tagFont.getAscent()));
            y += lineH;
        }
    }

    StageView& owner;
    Content content;
    mutable float meterDb[2] = { -60.0f, -60.0f };

    std::vector<Run> runs;
    std::vector<Mark> marks;
    std::vector<Bar> bars;
    std::vector<juce::Rectangle<float>> rowFills;
    std::vector<std::pair<juce::Rectangle<float>, int>> hits;   // set list rows (cue index), pedals (stomp number)
    juce::StringArray tagLines;
    juce::Rectangle<float> tagBox;
    juce::Font tagFont { juce::FontOptions() };

    juce::OwnedArray<kit::GearPlaceholder> pedals;
    std::unique_ptr<TunerDisplay> tuner;
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
        g.setColour (paint::c (colour::gridLine));

        for (int c = 1; c < stageGridColumns; ++c)
            g.fillRect (juce::Rectangle<float> ((float) c * cellWidth() - (float) size::gap * 0.5f, 0.0f, 1.0f, (float) getHeight()));

        for (int r = 1; r < stageGridRows; ++r)
            g.fillRect (juce::Rectangle<float> (0.0f, (float) r * cellHeight() - (float) size::gap * 0.5f, (float) getWidth(), 1.0f));

        for (int i = 0; i < (int) widgets.size(); ++i)
        {
            auto r = bounds (widgets[(size_t) i]).toFloat();
            const auto isSelected = i == selected;

            g.setColour (paint::c (isSelected ? colour::accent : colour::textDisabled));
            g.drawRoundedRectangle (r.reduced (isSelected ? 1.0f : 0.5f), (float) size::widgetRadius, isSelected ? 2.0f : 1.0f);

            if (isSelected)
            {
                g.setColour (paint::c (colour::accent));
                g.fillRect (r.removeFromRight ((float) handleSize).removeFromBottom ((float) handleSize));
            }
        }

        if (widgets.empty())
            paint::text (g, "Empty layout: \"+ Widget\" or double-click to add one", getLocalBounds().toFloat(),
                         type::subToolbarTitle, colour::textMuted, juce::Justification::centred);
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
            onAddAt ({ juce::jlimit (0, stageGridColumns - 1, (int) ((float) e.x / cellWidth())),
                       juce::jlimit (0, stageGridRows - 1, (int) ((float) e.y / cellHeight())) });
    }

    int selected = -1;

private:
    enum class Mode { none, move, resizeWidth, resizeHeight, resizeBoth };

    // A cell is (size + gap) / cells: the gap sits between widgets.
    float cellWidth() const  { return ((float) getWidth() + (float) size::gap) / (float) stageGridColumns; }
    float cellHeight() const { return ((float) getHeight() + (float) size::gap) / (float) stageGridRows; }

    juce::Rectangle<int> bounds (const StageWidget& w) const { return gridCell (getLocalBounds(), w); }

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

        juce::PopupMenu sizes;
        const std::pair<const char*, float> textSizes[] = { { "Small", 0.7f }, { "Normal", 1.0f }, { "Large", 1.4f },
                                                            { "Huge", 2.0f }, { "Biggest", 3.0f } };

        for (auto& [name, value] : textSizes)
            sizes.addItem (name, true, std::abs (widgets[(size_t) index].textScale - value) < 0.01f,
                           [change, index, value = value] { change ([index, value] (auto& w) { w[(size_t) index].textScale = value; }); });

        juce::PopupMenu menu;
        menu.addSectionHeader (describe (widgets[(size_t) index].type));
        menu.addSubMenu ("Text size", sizes);

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
    setOpaque (true);

    // Toolbar
    addAndMakeVisible (toolbar);
    layoutName = std::make_unique<Caption> (type::layoutName);
    hint = std::make_unique<Caption> (type::layoutName);
    miniMeters = std::make_unique<MiniMeters>();

    modeSwitch.setSegments ({ "Stage", "Edit", "Layout" });
    modeSwitch.setTooltips ({ {}, "Back to editing patches and the set list", "Move, resize, add and remove what this screen shows" });
    modeSwitch.setTitle ("Mode");
    modeSwitch.setSelected (0);
    modeSwitch.onClick = [this] (int index)
    {
        if (! locked)
        {
            if (index == 0 && editing)
                setEditingLayout (false);
            else if (index == 1 && onLeave != nullptr)
                onLeave();
            else if (index == 2 && ! editing)
                setEditingLayout (true);
        }

        modeSwitch.setSelected (editing ? 2 : 0);
    };

    setButtonIcon (lockButton, Icon::lock, (float) size::iconSize + 1.0f, false);
    lockButton.setTooltip ("Lock the screen so nothing changes by accident (footswitches keep working)");
    lockButton.onClick = [this] { if (! locked) setLocked (true); };
    lockButton.onStateChange = [this]
    {
        if (! locked)
            return;

        if (lockButton.isDown())
            beginUnlockPress();
        else
            endUnlockPress();
    };

    tunerButton.setTooltip ("Tuner (T)");
    tunerButton.onClick = [this] { engine.setTunerOpen (! engine.isTunerOpen()); };

    setVariant (muteButton, Variant::muted);
    muteButton.onClick = [this] { engine.setMuted (! engine.isMuted()); };

    layoutsButton.onClick = [this] { showLayoutMenu(); };
    setButtonIcon (layoutsButton, Icon::chevronDown, (float) size::fieldChevron, true);
    addWidgetButton.onClick = [this] { showAddWidgetMenu(); };
    setVariant (doneButton, Variant::primary);
    doneButton.onClick = [this] { setEditingLayout (false); };
    hint->setText ("Drag to move, drag the edges to resize, right-click for options, double-click empty space to add");

    for (auto* c : std::initializer_list<juce::Component*> { &modeSwitch, layoutName.get(), &lcd, miniMeters.get(), &lockButton,
                                                             &tunerButton, &muteButton, &layoutsButton, &addWidgetButton,
                                                             &doneButton, hint.get() })
    {
        noFocus (*c);
        toolbar.addAndMakeVisible (c);
    }

    for (int i = 0; i < modeSwitch.getNumSegments(); ++i)
        noFocus (*modeSwitch.getSegmentButton (i));

    // Strips
    mutedBar.setText (juce::String (juce::CharPointer_UTF8 ("Output muted \xe2\x80\x94 click to go live")));
    mutedBar.onClick = [this] { if (! locked) engine.setMuted (false); };

    for (auto* bar : { &lostBar, &noticeBar, &mutedBar })
    {
        noFocus (*bar);
        addChildComponent (bar);
    }

    footerHelp = "Lock: Ctrl+L" + middot() + "hold the lock 1 s to unlock" + middot() + "F11 full screen";

    noFocus (tunerOverlay);
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

bool StageView::layoutHas (StageWidgetType type) const
{
    return std::any_of (current.widgets.begin(), current.widgets.end(),
                        [type] (const StageWidget& w) { return w.type == type; });
}

int StageView::stripHeight (const kit::NoticeBar& bar) const
{
    return &bar == &mutedBar ? size::mutedBanner : size::stageBanner + 1;   // banners have a line under them
}

juce::Rectangle<int> StageView::getGridArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop (size::toolbarStage + 1);

    for (auto* bar : { &lostBar, &noticeBar, &mutedBar })
        if (bar->isVisible())
            area.removeFromTop (stripHeight (*bar));

    area.removeFromBottom (size::stageFooter + 1);
    return area.reduced (size::stageGridPad);
}

juce::Rectangle<int> StageView::cellBounds (const StageWidget& w) const
{
    return gridCell (getGridArea(), w);
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
    refreshToolbar();
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

    // Overruns in the last 10 s turn the health widget amber.
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

    refreshStrips (snapshot);

    for (auto* widget : widgets)
        widget->update (snapshot);

    // The tuner covers the grid when the layout has no tuner of its own.
    const auto overlay = engine.isTunerOpen() && ! layoutHas (StageWidgetType::tuner) && ! editing;

    if (overlay != tunerOverlay.isVisible())
        tunerOverlay.setVisible (overlay);

    if (overlay)
        tunerOverlay.setReading (snapshot.tuner, engine.getTunerMutes(), engine.getTuner().getReferencePitch());

    refreshToolbar();
    refreshFooter (snapshot);
    lostBar.advance();
}

void StageView::refreshStrips (const StageSnapshot& snapshot)
{
    const auto lost = snapshot.audioProblem.isNotEmpty();
    auto changed = false;

    const auto show = [&changed] (kit::NoticeBar& bar, bool visible)
    {
        if (bar.isVisible() != visible)
        {
            bar.setVisible (visible);
            changed = true;
        }
    };

    if (lost)
    {
        const auto cues = session.getCues();
        const auto index = session.getCurrentCue();
        juce::String staying;

        if (juce::isPositiveAndBelow (index, (int) cues.size()))
            staying = "Staying on " + session.getWords().item + " "
                      + (cues[(size_t) index]->number.isNotEmpty() ? cues[(size_t) index]->number : juce::String (index + 1)) + middot();

        lostBar.setText (snapshot.audioProblem, staying + "retrying every second" + middot() + "check the cable");
    }

    show (lostBar, lost);
    noticeBar.setText (snapshot.notice);
    show (noticeBar, snapshot.notice.isNotEmpty());
    show (mutedBar, engine.isMuted());

    if (changed)
        resized();
}

void StageView::refreshToolbar()
{
    // LCD: the item's number and word, its name and section path.
    const auto& words = session.getWords();
    const auto cues = session.getCues();
    const auto index = session.getCurrentCue();
    const auto* cue = juce::isPositiveAndBelow (index, (int) cues.size()) ? cues[(size_t) index] : nullptr;
    const auto* chain = engine.getChain();
    const auto dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
    const auto name = cue != nullptr ? cue->name : ! cues.empty() ? juce::String ("Ready") : (chain != nullptr ? chain->label : dash);
    const auto number = cue != nullptr ? (cue->number.isNotEmpty() ? cue->number : juce::String (index + 1)) : dash;

    const auto lcdWidth = lcd.getIdealWidth();
    lcd.setItems ({ { number, words.item, 0 }, { name, session.getSectionPath (index), size::lcdMinPatchStage } });

    const auto oldName = layoutName->getIdealWidth();
    layoutName->setText (juce::String (juce::CharPointer_UTF8 ("Layout \xe2\x80\x9c")) + current.name + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d")));

    miniMeters->setValues (juce::roundToInt (last.cpu * 100.0), last.overruns);

    const auto muted = engine.isMuted();

    if (muteButton.getToggleState() != muted)
    {
        muteButton.setToggleState (muted, juce::dontSendNotification);
        muteButton.setButtonText (muted ? "Muted" : "Mute");
    }

    tunerButton.setToggleState (engine.isTunerOpen(), juce::dontSendNotification);

    const auto lockText = locked ? "LOCKED: " + unlockProgressText() : juce::String();

    if (lockButton.getButtonText() != lockText)
        lockButton.setButtonText (lockText);

    // The lock button sits in the toolbar when the layout has no Lock widget, and while locked.
    const auto lockShown = ! editing && (locked || ! layoutHas (StageWidgetType::lockButton));

    if (lcd.getIdealWidth() != lcdWidth || layoutName->getIdealWidth() != oldName || lockButton.isVisible() != lockShown)
        layoutToolbar();
}

void StageView::refreshFooter (const StageSnapshot& snapshot)
{
    const auto alert = snapshot.audioProblem.isNotEmpty();

    if (snapshot.deviceLine == footerDevice && snapshot.footswitch == footerFootswitch && alert == footerAlert
        && ! footerHelpBox.isEmpty())
        return;

    footerDevice = snapshot.deviceLine;
    footerFootswitch = snapshot.footswitch;
    footerAlert = alert;

    // device line, 16, footswitch ... help, right-aligned (§3.4)
    auto row = getLocalBounds().removeFromBottom (size::stageFooter).toFloat().reduced ((float) size::stageFooterPadX, 0.0f);
    const auto helpW = juce::jmin (row.getWidth() * 0.5f, textWidth (font (type::stageFooter), footerHelp) + 1.0f);
    footerHelpBox = row.removeFromRight (helpW);
    row.removeFromRight ((float) size::stageFooterGap);
    const auto deviceW = juce::jmin (row.getWidth(), textWidth (font (alert ? type::stageFooterAlert : type::stageFooter), footerDevice) + 1.0f);
    footerDeviceBox = row.removeFromLeft (deviceW);
    row.removeFromLeft (deviceW > 0.0f ? (float) size::stageFooterGap : 0.0f);
    footerFootswitchBox = row;
    repaint (getLocalBounds().removeFromBottom (size::stageFooter + 1));
}

void StageView::setLocked (bool shouldLock)
{
    locked = shouldLock;
    unlockPressStart = 0;

    if (locked && editing)
        setEditingLayout (false);

    // Locked: everything in the toolbar but the lock itself is off.
    for (auto* c : std::initializer_list<juce::Component*> { &modeSwitch, &tunerButton, &muteButton })
        c->setEnabled (! locked);

    setVariant (lockButton, locked ? Variant::danger : Variant::standard);
    setButtonIcon (lockButton, Icon::lock, (float) size::iconSize + 1.0f, false);

    for (auto* widget : widgets)
        widget->update (last);

    refreshToolbar();
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

    modeSwitch.setSelected (editing ? 2 : 0);

    for (auto* c : std::initializer_list<juce::Component*> { &layoutsButton, &addWidgetButton, &doneButton, hint.get() })
        c->setVisible (editing);

    for (auto* c : std::initializer_list<juce::Component*> { &lcd, miniMeters.get() })
        c->setVisible (! editing);

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
    g.fillAll (paint::c (colour::window));

    // Footer (§3.4): fill, top line, device line, footswitch, help.
    const auto footer = getLocalBounds().removeFromBottom (size::stageFooter + 1).toFloat();
    g.setColour (paint::c (colour::stageFooter));
    g.fillRect (footer);
    g.setColour (paint::c (colour::paneBorder));
    paint::hline (g, footer.getX(), footer.getY(), footer.getWidth());

    paint::text (g, footerDevice, footerDeviceBox, footerAlert ? type::stageFooterAlert : type::stageFooter,
                 footerAlert ? colour::danger : colour::textMuted);
    paint::text (g, footerFootswitch, footerFootswitchBox, type::stageFooter, colour::textMuted);
    paint::text (g, footerHelp, footerHelpBox, type::stageFooter, colour::textMuted, juce::Justification::centredRight);
}

void StageView::layoutToolbar()
{
    const auto bar = toolbar.getLocalBounds();
    const auto h = size::toolbarStage;
    const auto buttonH = size::stageToolbarButtonH + size::buttonDrop;
    const auto buttonY = (h - size::stageToolbarButtonH) / 2;
    const auto smallH = size::buttonHeight + size::buttonDrop;
    const auto smallY = (h - size::buttonHeight) / 2;
    const auto gap = size::stageToolbarGap;

    auto left = bar.getX() + size::stageToolbarPadX;
    toolbar.setLogoBounds ({ left + size::logoMargin, (h - size::logoH) / 2, size::logoW, size::logoH });
    left += size::logoMargin + size::logoW + size::logoMargin + gap;

    const auto modeH = size::stageModeButtonH + 2;
    modeSwitch.setBounds (left, (h - modeH) / 2, size::stageModeSegmentW * 3 + 2, modeH);
    left = modeSwitch.getRight() + gap;

    layoutName->setBounds (left, 0, layoutName->getIdealWidth(), h);
    left = layoutName->getRight() + gap;

    auto right = bar.getRight() - size::stageToolbarPadX;
    const auto fromRight = [&right, gap] (juce::Component& c, int w, int height, int y)
    {
        right -= w;
        c.setBounds (right, y, w, height);
        right -= gap;
    };
    const auto padded = [] (juce::TextButton& b)
    {
        return juce::roundToInt (textWidth (font (type::button), b.getButtonText())) + 2 * size::stageButtonPadX + 2;
    };

    fromRight (muteButton, juce::jmax (size::stageMuteMinW, padded (muteButton)), buttonH, buttonY);
    fromRight (tunerButton, padded (tunerButton), buttonH, buttonY);

    const auto lockShown = ! editing && (locked || ! layoutHas (StageWidgetType::lockButton));
    lockButton.setVisible (lockShown);

    if (lockShown)
    {
        // Locked: wide enough for the hold progress, so it doesn't jump.
        const auto lockedW = juce::roundToInt (textWidth (font (type::button), "LOCKED: unlocking 100 %")) + 2 * size::stageButtonPadX
                             + juce::roundToInt ((float) size::iconSize + 1.0f) + size::stageTriangleGap + 2;
        fromRight (lockButton, locked ? lockedW : size::stageToolbarButtonH, buttonH, buttonY);
    }

    if (editing)
    {
        layoutName->setVisible (false);   // the layouts button names it
        fromRight (doneButton, juce::jmax (size::dialogButtonW, doneButton.getBestWidthForHeight (smallH)), smallH, smallY);
        layoutsButton.setButtonText ("Layout: " + current.name);
        const auto layoutsW = juce::jmin (layoutsButton.getBestWidthForHeight (smallH), juce::jmax (0, right - left));
        layoutsButton.setBounds (left, smallY, layoutsW, smallH);
        left = layoutsButton.getRight() + gap;
        addWidgetButton.setBounds (left, smallY, addWidgetButton.getBestWidthForHeight (smallH), smallH);
        left = addWidgetButton.getRight() + gap;
        hint->setBounds (left, 0, juce::jmax (0, right + gap - left), h);
        return;
    }

    // A narrow window drops the mini meters, then the layout name, then the LCD.
    miniMeters->setVisible (right - size::miniMetersW >= modeSwitch.getRight() + gap);

    if (miniMeters->isVisible())
        fromRight (*miniMeters, size::miniMetersW, h, 0);

    layoutName->setVisible (layoutName->getRight() <= right);
    left = layoutName->isVisible() ? layoutName->getRight() + gap : modeSwitch.getRight() + gap;

    // The LCD centred in the space between the two groups; hidden if it doesn't fit.
    const auto lcdW = lcd.getIdealWidth(), lcdH = kit::Lcd::heightFor (true);
    const auto space = right - left;
    lcd.setVisible (lcdW <= space);
    lcd.setBounds (left + (space - lcdW) / 2, (h - (lcdH - 1)) / 2, lcdW, lcdH);
}

void StageView::resized()
{
    contentScale = juce::jmax (0.1f, juce::jmin ((float) getWidth() / (float) size::referenceWidth,
                                                 (float) getHeight() / (float) size::referenceHeight));

    auto area = getLocalBounds();
    toolbar.setBounds (area.removeFromTop (size::toolbarStage + 1));
    layoutToolbar();

    for (auto* bar : { &lostBar, &noticeBar, &mutedBar })
        if (bar->isVisible())
            bar->setBounds (area.removeFromTop (stripHeight (*bar)));

    area.removeFromBottom (size::stageFooter + 1);

    if (editor != nullptr)
        editor->setBounds (getGridArea());

    tunerOverlay.setBounds (area);
    layoutWidgets();

    footerHelpBox = {};   // placed again
    refreshFooter (last);
    repaint();
}

} // namespace encore
