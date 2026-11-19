// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/MixerStrip.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace encore::kit
{

using namespace theme;

namespace
{
    constexpr int limitHoldFrames = 30;   // LIM stays lit ~1 s after the limiter lets go (30 Hz)

    juce::String middot() { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }
    juce::String minus()  { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }
}

//==============================================================================
TextCell::TextCell (const type::Style& s, juce::Justification j, int lines) : style (s), justification (j), maxLines (lines)
{
}

void TextCell::setText (const juce::String& newText, juce::uint32 newColour)
{
    if (newText != text || newColour != colour)
    {
        text = newText;
        colour = newColour;
        repaint();
    }
}

void TextCell::setDot (juce::uint32 newDot)
{
    if (newDot != dot)
    {
        dot = newDot;
        repaint();
    }
}

void TextCell::mouseUp (const juce::MouseEvent& e)
{
    if (onClick != nullptr && getLocalBounds().contains (e.getPosition()) && e.mouseWasClicked())
        onClick();
}

void TextCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    if (dot != 0)
    {
        const auto d = (float) size::limDotSize;
        g.setColour (paint::c (dot));
        g.fillEllipse (r.removeFromLeft (d).withSizeKeepingCentre (d, d));
        r.removeFromLeft ((float) size::limDotGap);
    }

    if (maxLines <= 1)
    {
        paint::text (g, text, r, style, colour, justification);
        return;
    }

    g.setColour (paint::c (colour));
    g.setFont (font (style));
    g.drawFittedText (text, r.toNearestInt(), justification, maxLines, 1.0f);
}

//==============================================================================
void PeakHold::feed (float linearPeak) noexcept
{
    peakDb = juce::jmax (peakDb, juce::Decibels::gainToDecibels (linearPeak, -100.0f));
}

juce::String PeakHold::text (bool muted) const
{
    if (muted || peakDb < dbfsLaw::floorDb)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));   // —

    return dbfsLaw::readout (peakDb, -100.0f);
}

juce::uint32 PeakHold::colour (bool muted) const noexcept
{
    return ! muted && peakDb >= -1.0f ? colour::peakHot : colour::stripValue;
}

//==============================================================================
MixerStrip::MixerStrip()
{
    addAndMakeVisible (nameCell);

    lim.setText (paint::caps (type::limLabel, "LIM"), colour::limText);
    lim.setDot (colour::limDot);
    lim.setTooltip ("Safety limiter at " + minus() + "1 dBFS");
    addAndMakeVisible (lim);

    peak.setTooltip ("Highest level since reset. Click to reset.");
    peak.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    peak.onClick = [this]
    {
        hold.reset();
        showPeak();

        if (onPeakReset != nullptr)
            onPeakReset();
    };
    addAndMakeVisible (peak);

    addAndMakeVisible (value);
    addAndMakeVisible (meter);

    fader.onChange = [this] (float dB)
    {
        showLevel (dB);

        if (onLevelChange != nullptr)
            onLevelChange (dB);
    };
    fader.onGestureEnd = [this]
    {
        if (onLevelCommit != nullptr)
            onLevelCommit (fader.getDb());
    };
    addAndMakeVisible (fader);

    mute.onClick = [this]
    {
        if (onMuteClicked != nullptr)
            onMuteClicked (mute.getToggleState());
    };
    addAndMakeVisible (mute);
    showPeak();
}

void MixerStrip::setOutput (const juce::String& newName, const juce::String& newChannels, bool stereo, float levelDb, bool isMuted)
{
    if (newName != name || newChannels != channels)
    {
        name = newName;
        channels = newChannels;
        setTitle ("Output " + name);
        nameCell.setText (name, colour::text);
        nameCell.setTooltip (name + middot() + "outputs " + channels);
        mute.setTitle ("Mute " + name);
        mute.setTooltip ("Mute " + name);
        fader.setTitle ("Level of " + name);
        fader.setTooltip ("Level of " + name + ". Double-click for 0 dB.");
    }

    if (isMuted != muted)
    {
        muted = isMuted;
        meter.setMuted (muted);
        showPeak();
    }

    meter.setNumChannels (stereo ? 2 : 1);

    if (! fader.isMouseButtonDown() && std::abs (fader.getDb() - levelDb) > 0.001f)
        fader.setDb (levelDb);

    showLevel (fader.getDb());

    if (mute.getToggleState() != isMuted)
        mute.setToggleState (isMuted, juce::dontSendNotification);
}

void MixerStrip::setFirst (bool isFirst)
{
    if (isFirst != first)
    {
        first = isFirst;
        resized();
        repaint();
    }
}

void MixerStrip::setPeaks (float left, float right)
{
    meter.setPeaks (left, right);

    if (! muted)
    {
        hold.feed (juce::jmax (left, right));
        showPeak();
    }
}

void MixerStrip::setLimiterReduction (float dB)
{
    if (dB > 0.05f)
        limitHold = limitHoldFrames;
    else if (limitHold > 0)
        --limitHold;

    const auto now = limitHold > 0;

    if (now != limiting)
    {
        limiting = now;
        lim.setText (lim.getText(), limiting ? colour::limOn : colour::limText);
        lim.setDot (limiting ? colour::limOn : colour::limDot);
        lim.setTooltip ("Safety limiter at " + minus() + "1 dBFS" + (limiting ? juce::String (" (working now)") : juce::String()));
    }
}

void MixerStrip::showLevel (float dB)
{
    value.setText (dbfsLaw::readout (dB, faderLaw::silentDb), muted ? colour::textDim : colour::stripValue);
}

void MixerStrip::showPeak()
{
    peak.setText (hold.text (muted), hold.colour (muted));
}

void MixerStrip::resized()
{
    using namespace size;
    auto area = getLocalBounds().withTrimmedLeft (first ? 0 : mixerPadX).withTrimmedRight (mixerPadX);
    const auto x0 = area.getX();
    const auto scaleX = x0, metersX = scaleX + mixerScaleW + mixerScaleGap, faderX = metersX + mixerMetersW + mixerMeterGap;

    nameCell.setBounds (area.removeFromTop (mixerNameH));
    area.removeFromTop (mixerRowGap);
    auto limRow = area.removeFromTop (mixerLimH);
    lim.setBounds (scaleX, limRow.getY(), faderX - mixerMeterGap - scaleX, limRow.getHeight());
    peak.setBounds (faderX, limRow.getY(), mixerFaderW, limRow.getHeight());
    area.removeFromTop (mixerRowGap);

    auto bottom = area.removeFromBottom (mixerBottomH);
    area.removeFromBottom (mixerRowGap);

    meter.setBounds (metersX, area.getY(), mixerMetersW, area.getHeight());
    fader.setBounds (faderX, area.getY(), mixerFaderW, area.getHeight());
    scaleColumn = juce::Rectangle<int> (scaleX, area.getY(), mixerScaleW, area.getHeight()).toFloat();

    value.setBounds (scaleX, bottom.getY(), metersX + mixerMetersW - scaleX, bottom.getHeight());
    mute.setBounds (juce::Rectangle<int> (outputMuteW, outputMuteH).withCentre ({ faderX + mixerFaderW / 2, bottom.getCentreY() }));
}

void MixerStrip::paint (juce::Graphics& g)
{
    dbfsLaw::paintScale (g, scaleColumn, meter.getBounds().toFloat());
}

//==============================================================================
InputStrip::InputStrip()
{
    picker.setCompact (true);
    addAndMakeVisible (picker);

    peak.setTooltip ("Highest level since reset. Click to reset.");
    peak.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    peak.onClick = [this]
    {
        hold.reset();
        peak.setText (hold.text (false), hold.colour (false));
    };
    peak.setText (hold.text (false), hold.colour (false));
    addAndMakeVisible (peak);

    meter.setNumChannels (1);
    addAndMakeVisible (meter);
    setTitle ("Input");
}

void InputStrip::setInput (const juce::String& name, const juce::String& caption, bool missing)
{
    picker.setText (name, caption, missing);
    const auto tip = name + (caption.isEmpty() ? juce::String() : middot() + caption)
                   + ". Input of the playing patch: pick another to change it for that patch.";

    if (picker.getTooltip() != tip)
        picker.setTooltip (tip);
}

void InputStrip::setPeak (float linear)
{
    meter.setPeaks (linear, 0.0f);
    hold.feed (linear);
    peak.setText (hold.text (false), hold.colour (false));
}

void InputStrip::resized()
{
    using namespace size;
    auto area = getLocalBounds().reduced (mixerPadX, 0).withWidth (mixerInputCols);
    labelArea = area.removeFromTop (mixerInputLabelH).toFloat();
    area.removeFromTop (mixerRowGap);
    picker.setBounds (area.removeFromTop (mixerInputPickerH));
    area.removeFromTop (mixerRowGap);
    const auto scaleX = area.getX(), meterX = scaleX + mixerScaleW + mixerScaleGap;
    // Right-aligned on the meter; it may use the padding on the left ("−10.5" is wider than the scale and the gap).
    peak.setBounds (area.removeFromTop (mixerInputPeakH).withRight (meterX + mixerInputMeterW).withLeft (0));
    area.removeFromTop (mixerRowGap);
    meter.setBounds (meterX, area.getY(), mixerInputMeterW, area.getHeight());
    scaleColumn = juce::Rectangle<int> (scaleX, area.getY(), mixerScaleW, area.getHeight()).toFloat();
}

void InputStrip::paint (juce::Graphics& g)
{
    paint::text (g, "Input", labelArea, type::label, colour::textTertiary);
    dbfsLaw::paintScale (g, scaleColumn, meter.getBounds().toFloat());
}

} // namespace encore::kit
