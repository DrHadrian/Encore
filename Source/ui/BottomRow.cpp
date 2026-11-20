// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BottomRow.h"

#include "ui/kit/Group.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

//==============================================================================
InputSummary::InputSummary()
{
    picker.setTooltip ("Input of the playing patch. Pick another to change it for that patch.");
    picker.onClick = [this]
    {
        if (onPickInput != nullptr)
            onPickInput (picker);
    };
    addAndMakeVisible (picker);

    meter.setBarThickness (size::inputMeterH);
    meter.setShowsPeak (true);
    addAndMakeVisible (meter);

    strip.getPicker().onClick = [this]
    {
        if (onPickInput != nullptr)
            onPickInput (strip.getPicker());
    };
    addChildComponent (strip);
    setTitle ("Input");
}

int InputSummary::mixerWidth() noexcept
{
    return kit::InputStrip::idealWidth() + 2;   // with the pane's border
}

void InputSummary::setInput (const juce::String& name, const juce::String& caption, bool missing)
{
    picker.setText (name, caption, missing);
    strip.setInput (name, caption, missing);
}

void InputSummary::setPeak (float linear)
{
    if (mixer)
        strip.setPeak (linear);
    else
        meter.setPeaks (linear, 0.0f);
}

void InputSummary::setMixer (bool m)
{
    if (m == mixer)
        return;

    mixer = m;
    picker.setVisible (! mixer);
    meter.setVisible (! mixer);
    strip.setVisible (mixer);
    resized();
    repaint();
}

void InputSummary::resized()
{
    strip.setBounds (getLocalBounds().reduced (1, 1 + size::bottomPanePad));

    // Collapsed: label, picker, meter (gap 5), centred.
    const auto area = getLocalBounds().reduced (1 + size::bottomPanePad);
    const auto gap = size::bottomLineGap;
    const auto blockH = size::bottomLineH + gap + size::inputPickerH + gap + size::inputMeterH;
    auto block = area.withSizeKeepingCentre (area.getWidth(), blockH);
    labelArea = block.removeFromTop (size::bottomLineH).toFloat();
    block.removeFromTop (gap);
    picker.setBounds (block.removeFromTop (size::inputPickerH));
    block.removeFromTop (gap);
    meter.setBounds (block.removeFromTop (size::inputMeterH));
}

void InputSummary::paint (juce::Graphics& g)
{
    if (! mixer)
        paint::text (g, "Input", labelArea, type::label, colour::textTertiary);
}

//==============================================================================
/** The strips' row: the 1 px dividers between the visible strips. */
class OutputsPane::Row final : public juce::Component
{
public:
    Row() { setInterceptsMouseClicks (false, true); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (paint::c (colour::stripDivider));
        auto first = true;

        for (auto* c : getChildren())
        {
            if (! c->isVisible())
                continue;

            if (! first)
                g.fillRect (c->getX(), 0, 1, getHeight());

            first = false;
        }
    }
};

OutputsPane::OutputsPane() : row (std::make_unique<Row>())
{
    setTitle ("Outputs");
    setTooltip ("Outputs on this computer. M mutes one.");
    viewport.setViewedComponent (row.get(), false);
    viewport.setScrollBarsShown (false, false, false, true);   // the wheel scrolls a wide mixer view sideways
    addAndMakeVisible (viewport);
}

OutputsPane::~OutputsPane()
{
    viewport.setViewedComponent (nullptr, false);
}

void OutputsPane::setOutputs (const std::vector<Output>& newOutputs)
{
    auto rebuildNeeded = newOutputs.size() != outputs.size();

    for (size_t i = 0; ! rebuildNeeded && i < newOutputs.size(); ++i)
        rebuildNeeded = newOutputs[i].name != outputs[i].name;

    outputs = newOutputs;

    if (rebuildNeeded)
        rebuild();

    for (size_t i = 0; i < outputs.size(); ++i)
    {
        const auto& o = outputs[i];
        strips[(int) i]->setOutput (o.name, o.channels, o.stereo, o.levelText, o.muted);
        mixerStrips[(int) i]->setOutput (o.name, o.channels, o.stereo, o.levelDb, o.muted);
    }
}

void OutputsPane::rebuild()
{
    strips.clear();
    mixerStrips.clear();

    for (int i = 0; i < (int) outputs.size(); ++i)
    {
        auto* s = strips.add (std::make_unique<kit::OutputStrip>());
        s->onMuteClicked = [this, i] (bool m)
        {
            if (onMuteClicked != nullptr)
                onMuteClicked (i, m);
        };
        row->addChildComponent (s);

        auto* m = mixerStrips.add (std::make_unique<kit::MixerStrip>());
        m->setFirst (i == 0);
        m->onMuteClicked = [this, i] (bool muted)
        {
            if (onMuteClicked != nullptr)
                onMuteClicked (i, muted);
        };
        m->onLevelChange = [this, i] (float dB)
        {
            if (onLevelChange != nullptr)
                onLevelChange (i, dB);
        };
        m->onLevelCommit = [this, i] (float dB)
        {
            if (onLevelCommit != nullptr)
                onLevelCommit (i, dB);
        };
        m->onPeakReset = [this, i]
        {
            if (onPeakReset != nullptr)
                onPeakReset (i);
        };
        row->addChildComponent (m);
    }

    layoutRow();
}

void OutputsPane::setMixer (bool m)
{
    if (m != mixer)
    {
        mixer = m;
        layoutRow();
    }
}

void OutputsPane::setPeaks (int index, float left, float right)
{
    if (mixer)
    {
        if (auto* m = mixerStrips[index])
            m->setPeaks (left, right);
    }
    else if (auto* s = strips[index])
    {
        s->setPeaks (left, right);
    }
}

void OutputsPane::setLimiterReduction (int index, float dB)
{
    if (auto* m = mixerStrips[index])
        m->setLimiterReduction (dB);
}

void OutputsPane::resized()
{
    viewport.setBounds (getLocalBounds().reduced (size::bottomPanePad));
    layoutRow();
}

void OutputsPane::layoutRow()
{
    const auto h = viewport.getHeight();
    const auto n = (int) outputs.size();

    for (auto* s : strips)
        s->setVisible (! mixer);

    for (auto* m : mixerStrips)
        m->setVisible (mixer);

    if (mixer)
    {
        auto x = 0;

        for (int i = 0; i < n; ++i)
        {
            const auto w = kit::MixerStrip::widthFor (i == 0);
            mixerStrips[i]->setBounds (x, 0, w, h);
            x += w;
        }

        row->setBounds (0, 0, juce::jmax (x, viewport.getWidth()), h);
    }
    else
    {
        // Equal shares; 12 px inside each, none at the row's two ends.
        const auto w = viewport.getWidth();
        row->setBounds (0, 0, w, h);

        for (int i = 0; i < n; ++i)
        {
            const auto x0 = w * i / juce::jmax (1, n), x1 = w * (i + 1) / juce::jmax (1, n);
            strips[i]->setPadding (i == 0 ? 0 : size::outputStripPad, i == n - 1 ? 0 : size::outputStripPad);
            strips[i]->setBounds (x0, 0, x1 - x0, h);
        }

        viewport.setViewPosition (0, 0);
    }

    row->repaint();
}

//==============================================================================
/** The window's body: the summary painted, the groups as kit::Group boxes,
    their rows painted over them. Everything is laid out when the report
    changes, so paint() only draws. */
class HealthDetailsDialog::Content final : public juce::Component,
                                           public juce::TooltipClient
{
public:
    Content()
    {
        for (auto* group : { &toCheckGroup, &audioGroup, &deviceGroup, &computerGroup })
            addChildComponent (group);   // they take no clicks: the tooltips are ours
    }

    void setReport (const HealthReport& newReport)
    {
        if (newReport == report && laidOut)
            return;

        report = newReport;
        layout();
        repaint();
    }

    const HealthReport& getReport() const noexcept { return report; }

    /** The body's height at the current width. */
    int getIdealHeight() const noexcept { return idealHeight; }

    void resized() override
    {
        layout();
    }

    void paint (juce::Graphics& g) override
    {
        const auto problems = report.toCheck.size();

        // The badge: amber with "!", or green with a tick.
        g.setColour (paint::c (problems > 0 ? colour::warnBadgeFill : colour::okBadgeFill));
        g.fillEllipse (badge.toFloat());
        const auto mark = badge.toFloat().withSizeKeepingCentre ((float) size::healthBadgeIcon, (float) size::healthBadgeIcon);

        if (problems > 0)
        {
            g.setColour (paint::c (colour::amber));
            g.fillPath (exclamation);
        }
        else
        {
            drawIcon (g, Icon::check, mark, paint::c (colour::healthy));
        }

        paint::text (g, summaryTitle, title.toFloat(), type::healthSummary, problems > 0 ? colour::amber : colour::text);
        paint::text (g, report.summary, sub.toFloat(), type::subtitle, colour::textSecondary);

        g.setColour (paint::c (colour::healthy));
        g.fillEllipse (liveDot.toFloat());
        paint::text (g, "Live", liveText.toFloat(), type::caption, colour::textMuted);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        for (const auto& line : checkLines)
        {
            drawIcon (g, Icon::warn, line.icon.toFloat(), paint::c (colour::amber));
            paint::text (g, line.text, line.textArea.toFloat(), type::body, colour::amber);
        }

        for (const auto& row : rows)
        {
            paint::text (g, row.label, row.labelArea.toFloat(), type::bodySmall, colour::textSecondary);
            paint::text (g, row.value, row.valueArea.toFloat(), type::healthValue, row.problem ? colour::amber : colour::text,
                         juce::Justification::centredRight);
        }

        for (const auto& tick : ticks)
        {
            drawIcon (g, tick.fine ? Icon::check : Icon::warn, tick.icon.toFloat(), paint::c (tick.fine ? colour::healthy : colour::amber));
            paint::text (g, tick.text, tick.textArea.toFloat(), type::bodySmall, colour::text);
        }

        if (! note.isEmpty())
        {
            g.setColour (paint::c (colour::textMuted));
            g.setFont (font (type::caption));
            g.drawFittedText (noteText, note, juce::Justification::topLeft, 3, 1.0f);
        }
    }

    juce::String getTooltip() override
    {
        const auto at = getMouseXYRelative();

        for (const auto& tip : tips)
            if (tip.area.contains (at))
                return tip.text;

        return {};
    }

private:
    struct CheckLine { juce::Rectangle<int> icon, textArea; juce::String text; };
    struct RowLine   { juce::Rectangle<int> labelArea, valueArea; juce::String label, value; bool problem = false; };
    struct TickLine  { juce::Rectangle<int> icon, textArea; juce::String text; bool fine = true; };
    struct Tip       { juce::Rectangle<int> area; juce::String text; };

    HealthReport report;
    bool laidOut = false;
    int idealHeight = 0;

    juce::String summaryTitle;
    juce::Rectangle<int> badge, title, sub, liveDot, liveText, note;
    juce::Path exclamation;
    const juce::String noteText { "Counts since the last reset. Reset, then play the show's heaviest patches." };

    std::vector<CheckLine> checkLines;
    std::vector<RowLine> rows;
    std::vector<TickLine> ticks;
    std::vector<Tip> tips;

    kit::Group toCheckGroup { "To check" }, audioGroup { "Audio" }, deviceGroup { "Device" }, computerGroup { "Computer" };

    static int rowsBoxHeight (int numRows) noexcept
    {
        // Rows 22 apart, 11 above the first and below the last line (15 high).
        return numRows > 0 ? 2 * size::healthBoxPadY + (numRows - 1) * size::healthRowH + size::healthRowLineH : 0;
    }

    void layout()
    {
        laidOut = true;
        checkLines.clear();
        rows.clear();
        ticks.clear();
        tips.clear();

        const auto pad = size::healthDialogPad;
        const auto width = getWidth() - 2 * pad;
        const auto problems = report.toCheck.size();

        // Summary.
        summaryTitle = problems == 0 ? juce::String ("Healthy")
                                     : juce::String (problems) + (problems == 1 ? " thing to check" : " things to check");
        const auto summaryTop = size::healthTitleTop;
        const auto summaryBottom = size::healthSubTop + size::healthSubH;
        badge = { pad, (summaryTop + summaryBottom - size::healthBadge) / 2, size::healthBadge, size::healthBadge };
        const auto textX = pad + size::healthBadge + size::healthBadgeGap;
        const auto liveW = juce::roundToInt (textWidth (font (type::caption), "Live")) + 1;
        const auto liveRight = pad + width;
        const auto liveTop = (summaryTop + summaryBottom - size::healthLiveH) / 2;
        liveText = { liveRight - liveW, liveTop, liveW, size::healthLiveH };
        liveDot = { liveText.getX() - size::healthLiveGap - size::healthLiveDot, liveTop + (size::healthLiveH - size::healthLiveDot) / 2,
                    size::healthLiveDot, size::healthLiveDot };
        tips.push_back ({ liveDot.getUnion (liveText), "Updates while this window is open" });
        title = { textX, size::healthTitleTop, liveDot.getX() - 8 - textX, size::healthTitleH };
        sub = { textX, size::healthSubTop, title.getWidth(), size::healthSubH };

        {
            // "!": a 3 px stroke and a dot, in the badge's 18 px box (24 px viewBox: 12,6 → 12,13; dot at 17.5).
            const auto box = badge.toFloat().withSizeKeepingCentre ((float) size::healthBadgeIcon, (float) size::healthBadgeIcon);
            const auto unit = box.getWidth() / 24.0f;
            juce::Path line;
            line.startNewSubPath (box.getCentreX(), box.getY() + 6.0f * unit);
            line.lineTo (box.getCentreX(), box.getY() + 13.0f * unit);
            exclamation.clear();
            juce::PathStrokeType (size::healthBadgeMark, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (exclamation, line);
            exclamation.addEllipse (box.getCentreX() - size::healthBadgeMark * 0.5f, box.getY() + 17.5f * unit - size::healthBadgeMark * 0.5f,
                                    size::healthBadgeMark, size::healthBadgeMark);
        }

        auto y = size::healthGroupsTop;

        // To check: one 26 px row each, only when there's something.
        toCheckGroup.setVisible (problems > 0);

        if (problems > 0)
        {
            const auto boxH = 2 * size::healthCheckPadY + problems * size::healthCheckRowH;
            toCheckGroup.setBounds (pad, y, width, kit::Group::heightFor (boxH));
            const auto box = toCheckGroup.getBoxBounds() + toCheckGroup.getPosition();

            for (int i = 0; i < problems; ++i)
            {
                const auto row = juce::Rectangle<int> (box.getX(), box.getY() + size::healthCheckPadY + i * size::healthCheckRowH,
                                                       box.getWidth(), size::healthCheckRowH);
                CheckLine line;
                line.text = report.toCheck[i];
                line.icon = juce::Rectangle<int> (row.getX() + size::healthCheckIconX, 0, size::healthCheckIcon, size::healthCheckIcon)
                                .withCentre ({ row.getX() + size::healthCheckIconX + size::healthCheckIcon / 2, row.getCentreY() });
                const auto textX2 = line.icon.getRight() + size::healthCheckGap;
                line.textArea = { textX2, row.getY(), row.getRight() - size::healthCheckIconX - textX2, row.getHeight() };
                checkLines.push_back (line);
                tips.push_back ({ row, line.text });   // the full text, when it's cut
            }

            y = toCheckGroup.getBottom() + size::healthGroupGap;
        }

        // Two columns: Audio (and its note) | Device, then Computer.
        const auto colW = (width - size::settingsColumnGap) / 2;
        const auto addRows = [this] (kit::Group& group, int x, int top, int w, const std::vector<HealthReport::Row>& lines)
        {
            group.setVisible (! lines.empty());
            group.setBounds (x, top, w, kit::Group::heightFor (rowsBoxHeight ((int) lines.size())));
            const auto box = group.getBoxBounds() + group.getPosition();
            const auto inner = box.reduced (size::healthRowPadX, 0);

            for (size_t i = 0; i < lines.size(); ++i)
            {
                const auto rowY = box.getY() + size::healthBoxPadY + (int) i * size::healthRowH;
                const auto labelW = juce::roundToInt (textWidth (font (type::bodySmall), lines[i].label)) + 1;
                RowLine row;
                row.label = lines[i].label;
                row.value = lines[i].value;
                row.problem = lines[i].problem;
                row.labelArea = { inner.getX(), rowY, juce::jmin (labelW, inner.getWidth()), size::healthRowLineH };
                const auto valueX = row.labelArea.getRight() + size::healthCheckGap + 4;   // 12 between label and value
                row.valueArea = { valueX, rowY, juce::jmax (0, inner.getRight() - valueX), size::healthRowLineH };
                rows.push_back (row);
                tips.push_back ({ row.labelArea.getUnion (row.valueArea), row.label + ": " + row.value });
            }

            return group.isVisible() ? group.getBottom() : top;
        };

        const auto leftX = pad, rightX = pad + colW + size::settingsColumnGap;
        const auto audioBottom = addRows (audioGroup, leftX, y, colW, report.audio);
        note = { leftX, audioBottom + size::healthNoteGap, colW,
                 2 * juce::roundToInt (font (type::caption).getHeight() * 1.45f) };
        const auto deviceBottom = addRows (deviceGroup, rightX, y, colW, report.device);

        computerGroup.setVisible (! report.computer.empty());
        auto rightBottom = deviceBottom;

        if (! report.computer.empty())
        {
            const auto top = deviceBottom + size::healthGroupGap;
            computerGroup.setBounds (rightX, top, colW, kit::Group::heightFor (rowsBoxHeight ((int) report.computer.size())));
            const auto box = computerGroup.getBoxBounds() + computerGroup.getPosition();

            for (size_t i = 0; i < report.computer.size(); ++i)
            {
                const auto rowY = box.getY() + size::healthBoxPadY + (int) i * size::healthRowH;
                TickLine tick;
                tick.text = report.computer[i].text;
                tick.fine = report.computer[i].fine;
                tick.icon = juce::Rectangle<int> (box.getX() + size::healthRowPadX, rowY + (size::healthRowLineH - size::healthTickIcon) / 2,
                                                  size::healthTickIcon, size::healthTickIcon);
                const auto tx = tick.icon.getRight() + size::healthTickGap;
                tick.textArea = { tx, rowY, box.getRight() - size::healthRowPadX - tx, size::healthRowLineH };
                ticks.push_back (tick);
                tips.push_back ({ tick.icon.getUnion (tick.textArea), tick.text });
            }

            rightBottom = computerGroup.getBottom();
        }

        idealHeight = juce::jmax (note.getBottom(), rightBottom) + size::healthGroupGap;   // the board leaves 18 under Computer: no scroll at 640 x 492
    }
};

/** The frame's body: the content, in a viewport that scrolls when needed. */
class HealthDetailsDialog::Body final : public juce::Component
{
public:
    Body()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    void setReport (const HealthReport& report)
    {
        const auto before = content.getIdealHeight();
        content.setReport (report);

        if (content.getIdealHeight() != before)
            resized();
    }

    const HealthReport& getReport() const noexcept { return content.getReport(); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const auto scrolls = content.getIdealHeight() > getHeight();
        const auto w = getWidth() - (scrolls ? viewport.getScrollBarThickness() : 0);
        content.setSize (w, 1);   // lays out at the width, giving the height
        content.setSize (w, juce::jmax (getHeight(), content.getIdealHeight()));
    }

private:
    juce::Viewport viewport;
    Content content;
};

HealthDetailsDialog::HealthDetailsDialog (std::function<void()> resetCounters, std::function<void()> close)
    : frame ("System health", "Close system health"), body (std::make_unique<Body>())
{
    frame.setContent (body.get());
    auto& reset = frame.addFooterButton ("Reset counters");
    reset.setTooltip ("Start the overrun, dropout and clip counts again from zero");
    reset.onClick = std::move (resetCounters);
    frame.onClose = close;
    auto& done = frame.addFooterButton ("Done", Variant::primary);
    done.onClick = std::move (close);
    overlay = std::make_unique<kit::DialogOverlay> (frame, juce::Rectangle<int> (size::healthDialogW + 2, size::healthDialogH + 2));
}

HealthDetailsDialog::~HealthDetailsDialog() = default;

void HealthDetailsDialog::setReport (const HealthReport& report)
{
    body->setReport (report);
}

const HealthReport& HealthDetailsDialog::getReport() const noexcept
{
    return body->getReport();
}

} // namespace encore
