// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/OutputStrip.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

OutputStrip::OutputStrip()
{
    bars.setBarThickness (size::levelBarH);
    bars.setGap (size::levelBarGap);
    addAndMakeVisible (bars);

    mute.onClick = [this]
    {
        if (onMuteClicked != nullptr)
            onMuteClicked (mute.getToggleState());
    };
    addAndMakeVisible (mute);
}

void OutputStrip::setOutput (const juce::String& newName, const juce::String& newChannels, bool stereo, const juce::String& levelText, bool isMuted)
{
    if (newName != name || newChannels != channels || levelText != value || isMuted != muted)
    {
        const auto relayout = newChannels != channels;
        name = newName;
        channels = newChannels;
        value = levelText;
        muted = isMuted;
        setTitle ("Output " + name);
        setTooltip (name);
        mute.setTitle ("Mute " + name);
        mute.setTooltip ("Mute " + name);
        channelsWidth = textWidth (font (type::outputChannels), channels) + 1.0f;

        if (relayout)
            resized();

        repaint();
    }

    bars.setNumChannels (stereo ? 2 : 1);
    bars.setMuted (isMuted);

    if (mute.getToggleState() != isMuted)
        mute.setToggleState (isMuted, juce::dontSendNotification);
}

void OutputStrip::setPadding (int left, int right)
{
    if (left != padLeft || right != padRight)
    {
        padLeft = left;
        padRight = right;
        resized();
        repaint();
    }
}

void OutputStrip::resized()
{
    const auto lineH = size::bottomLineH, gap = size::bottomLineGap, rowH = size::outputMuteH;
    auto area = getLocalBounds().withTrimmedLeft (padLeft).withTrimmedRight (padRight);
    valueOwnLine = area.getWidth() < size::outputInlineMinW;

    // Two lines (three when narrow), centred in the strip.
    const auto blockH = lineH + gap + rowH + (valueOwnLine ? gap + lineH : 0);
    auto block = area.withSizeKeepingCentre (area.getWidth(), blockH);

    auto first = block.removeFromTop (lineH).toFloat();
    channelsArea = first.removeFromRight (channelsWidth);
    first.removeFromRight ((float) size::outputInlineGap);
    nameArea = first;
    block.removeFromTop (gap);

    auto row = block.removeFromTop (rowH);
    mute.setBounds (row.removeFromRight (size::outputMuteW));
    row.removeFromRight (size::outputInlineGap);

    if (! valueOwnLine)
    {
        valueArea = row.removeFromRight (size::outputValueW).toFloat();
        row.removeFromRight (size::outputInlineGap);
    }
    else
    {
        block.removeFromTop (gap);
        valueArea = block.removeFromTop (lineH).toFloat();
    }

    const auto barsH = 2 * size::levelBarH + size::levelBarGap;
    bars.setBounds (row.withSizeKeepingCentre (juce::jmax (0, row.getWidth()), barsH));
}

void OutputStrip::paint (juce::Graphics& g)
{
    paint::text (g, channels, channelsArea, type::outputChannels, colour::textDim, juce::Justification::centredRight);
    // Squeezed up to 4 % before it's shortened (font metrics differ a little between renderers).
    g.setColour (paint::c (colour::text));
    g.setFont (font (type::outputName));
    g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, size::nameMinSqueeze);

    paint::text (g, value, valueArea, type::outputValue, muted ? colour::textDim : colour::stripValue,
                 valueOwnLine ? juce::Justification::centredLeft : juce::Justification::centredRight);
}

} // namespace encore::kit
