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
    const auto newValue = isMuted ? juce::String ("muted") : levelText;

    if (newName != name || newChannels != channels || newValue != value || isMuted != muted)
    {
        name = newName;
        channels = newChannels;
        value = newValue;
        muted = isMuted;
        setTitle ("Output " + name);
        mute.setTitle ("Mute " + name);
        repaint();
    }

    bars.setNumChannels (stereo ? 2 : 1);
    bars.setMuted (isMuted);

    if (mute.getToggleState() != isMuted)
        mute.setToggleState (isMuted, juce::dontSendNotification);
}

void OutputStrip::resized()
{
    const auto pad = size::outputStripPadX;
    mute.setBounds (getWidth() - pad - size::outputMuteW, size::outputStripMuteTop, size::outputMuteW, size::outputMuteH);
    const auto barsH = 2 * size::levelBarH + size::levelBarGap;
    bars.setBounds (pad, size::outputStripBarsTop, juce::jmax (0, mute.getX() - 6 - pad), barsH);
}

void OutputStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (paint::c (colour::outputStripBg));
    g.fillRoundedRectangle (r, (float) size::outputStripRadius);
    g.setColour (paint::c (colour::paneHeaderLine));
    g.drawRoundedRectangle (r.reduced (0.5f), (float) size::outputStripRadius - 0.5f, 1.0f);

    const auto pad = (float) size::outputStripPadX;
    auto top = juce::Rectangle<float> (pad, (float) size::outputStripNameTop, r.getWidth() - 2.0f * pad, (float) size::statusTextH - 1.0f);
    const auto channelsW = textWidth (font (type::outputChannels), channels) + 1.0f;
    paint::text (g, channels, top.removeFromRight (channelsW), type::outputChannels, colour::textDisabled, juce::Justification::centredRight);
    top.removeFromRight (6.0f);
    // Squeezed up to 4 % before it's shortened (font metrics differ a little between renderers).
    g.setColour (paint::c (colour::text));
    g.setFont (font (type::outputName));
    g.drawFittedText (name, top.toNearestInt(), juce::Justification::centredLeft, 1, size::nameMinSqueeze);

    paint::text (g, value, { pad, (float) size::outputStripValueTop, r.getWidth() - 2.0f * pad, (float) size::statusTextH - 2.0f },
                 type::outputValue, muted ? colour::danger : colour::textTertiary);
}

} // namespace encore::kit
