// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BottomRow.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

//==============================================================================
InputSummary::InputSummary()
{
    setInterceptsMouseClicks (false, false);
    meter.setBarThickness (size::inputSummaryMeterH);
    meter.setShowsPeak (true);
    addAndMakeVisible (meter);
}

void InputSummary::setInput (const juce::String& newName, const juce::String& newCaption, bool isMissing)
{
    if (newName == name && newCaption == caption && isMissing == missing)
        return;

    name = newName;
    caption = newCaption;
    missing = isMissing;
    nameWidth = textWidth (font (type::inputSummaryName), name);
    setTitle ("Input " + name + " " + caption);
    repaint();
}

void InputSummary::resized()
{
    meter.setBounds (size::inputPadX, size::inputSummaryMeterTop, getWidth() - 2 * size::inputPadX, size::inputSummaryMeterH);
}

void InputSummary::paint (juce::Graphics& g)
{
    const auto x = (float) size::inputPadX;
    const auto w = (float) (getWidth() - 2 * size::inputPadX);
    const auto top = juce::Rectangle<float> (x, (float) size::inputSummaryLabelTop, w, (float) size::statusTextH - 1.0f);
    paint::text (g, "Input", top, type::label, colour::textTertiary);
    paint::text (g, "playing patch", top, type::saveState, colour::textDisabled, juce::Justification::centredRight);

    auto line = juce::Rectangle<float> (x, (float) size::inputSummaryNameTop, w, (float) size::statusTextH + 1.0f);
    const auto nameW = juce::jmin (nameWidth, line.getWidth());
    paint::text (g, name, line.removeFromLeft (nameW), type::inputSummaryName, missing ? colour::amber : colour::text);
    line.removeFromLeft (6.0f);
    paint::text (g, caption, line, type::caption, colour::textMuted);
}

//==============================================================================
OutputsPane::OutputsPane()
{
    mode.setSegments ({ "Compact", "Full" });
    mode.setSegmentWidths ({ size::outputsSegmentCompactW, size::outputsSegmentW - size::outputsSegmentCompactW });
    mode.setTooltips ({ "Strips share the width", "170 px strips; the row scrolls sideways" });
    mode.onClick = [this] (int index)
    {
        setFull (index == 1);

        if (onFullChanged != nullptr)
            onFullChanged (full);
    };
    mode.setSelected (0);
    addAndMakeVisible (mode);

    viewport.setViewedComponent (&row, false);
    viewport.setScrollBarsShown (false, true, false, true);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
}

void OutputsPane::setOutputs (const std::vector<Output>& newOutputs)
{
    if (newOutputs == outputs)
        return;

    const auto sameNames = newOutputs.size() == outputs.size()
                           && std::equal (newOutputs.begin(), newOutputs.end(), outputs.begin(),
                                          [] (const Output& a, const Output& b) { return a.name == b.name; });
    outputs = newOutputs;

    if (! sameNames)
    {
        strips.clear();

        for (int i = 0; i < (int) outputs.size(); ++i)
        {
            auto* strip = strips.add (new kit::OutputStrip());
            strip->onMuteClicked = [this, i] (bool muted)
            {
                if (onMuteClicked != nullptr)
                    onMuteClicked (i, muted);
            };
            row.addAndMakeVisible (strip);
        }

        resized();
    }

    for (int i = 0; i < (int) outputs.size(); ++i)
    {
        const auto& o = outputs[(size_t) i];
        strips[i]->setOutput (o.name, o.channels, o.stereo, o.levelText, o.muted);
    }

    const auto count = (int) outputs.size();
    note = juce::String (count) + " on this computer" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "M mutes one";
    repaint();
}

void OutputsPane::setFull (bool shouldBeFull)
{
    mode.setSelected (shouldBeFull ? 1 : 0);

    if (shouldBeFull != full)
    {
        full = shouldBeFull;
        resized();
    }
}

void OutputsPane::setPeaks (int index, float left, float right)
{
    if (auto* strip = strips[index])
        strip->setPeaks (left, right);
}

void OutputsPane::resized()
{
    const auto pad = size::outputStripPadX;
    mode.setBounds (getWidth() - pad - size::outputsSegmentW, size::outputsHeaderTop - 4, size::outputsSegmentW, size::outputsSegmentH);

    const auto area = juce::Rectangle<int> (pad, size::outputsPaneTop, getWidth() - 2 * pad, size::outputStripH);
    viewport.setBounds (area.withHeight (getHeight() - size::outputsPaneTop));

    const auto n = strips.size();
    const auto gap = size::outputStripGap;

    if (n == 0)
    {
        row.setSize (area.getWidth(), size::outputStripH);
        return;
    }

    // Compact: equal shares of the width. Full: 170 each, the row scrolls.
    const auto stripW = full ? (float) size::outputStripFullW : (float) (area.getWidth() - (n - 1) * gap) / (float) n;
    const auto rowW = full ? juce::roundToInt (stripW * (float) n + (float) ((n - 1) * gap)) : area.getWidth();
    row.setSize (rowW, size::outputStripH);

    for (int i = 0; i < n; ++i)
    {
        const auto x = juce::roundToInt ((stripW + (float) gap) * (float) i);
        const auto right = juce::roundToInt ((stripW + (float) gap) * (float) i + stripW);
        strips[i]->setBounds (x, 0, right - x, size::outputStripH);
    }
}

void OutputsPane::paint (juce::Graphics& g)
{
    const auto pad = (float) size::outputStripPadX;
    const auto head = juce::Rectangle<float> (pad, (float) size::outputsHeaderTop, (float) getWidth() - 2.0f * pad, 13.0f);
    const auto caps = juce::String ("OUTPUTS");
    paint::text (g, caps, head, type::paneHeader, colour::textMuted);
    const auto w = textWidth (font (type::paneHeader), caps);
    paint::text (g, note, head.withTrimmedLeft (w + 8.0f), type::saveState, colour::textDisabled);
}

} // namespace encore
