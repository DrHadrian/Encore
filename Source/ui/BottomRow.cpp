// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BottomRow.h"

#include "ui/theme/EncoreFonts.h"
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
class HealthDetailsDialog::Content final : public juce::Component
{
public:
    void setLines (const std::vector<Line>& lines)
    {
        while (cells.size() > (int) lines.size())
            cells.removeLast();

        while (cells.size() < (int) lines.size())
            addAndMakeVisible (cells.add (std::make_unique<kit::TextCell> (type::body)));

        for (size_t i = 0; i < lines.size(); ++i)
        {
            auto* c = cells[(int) i];
            c->setText (lines[i].text, lines[i].problem ? colour::amber : colour::textSecondary);

            if (c->getTooltip() != lines[i].tooltip)
                c->setTooltip (lines[i].tooltip);
        }

        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (size::healthDialogPad);

        for (auto* c : cells)
            c->setBounds (r.removeFromTop (size::healthDialogLineH));
    }

    juce::OwnedArray<kit::TextCell> cells;
};

HealthDetailsDialog::HealthDetailsDialog (std::function<void()> resetCounters, std::function<void()> close)
    : frame ("System health", "Close system health"), content (std::make_unique<Content>())
{
    frame.setContent (content.get());
    auto& reset = frame.addFooterButton ("Reset counters");
    reset.setTooltip ("Start the overrun, dropout and clip counts again from zero");
    reset.onClick = std::move (resetCounters);
    frame.onClose = close;
    auto& done = frame.addFooterButton ("Done", Variant::primary);
    done.onClick = std::move (close);
    overlay = std::make_unique<kit::DialogOverlay> (frame, juce::Rectangle<int> (size::healthDialogW + 2, size::healthDialogH + 2));
}

HealthDetailsDialog::~HealthDetailsDialog() = default;

void HealthDetailsDialog::setLines (const std::vector<Line>& lines)
{
    content->setLines (lines);
}

} // namespace encore
