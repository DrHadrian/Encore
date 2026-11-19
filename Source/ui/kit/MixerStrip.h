// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "ui/kit/DbfsMeter.h"
#include "ui/kit/Fader.h"
#include "ui/kit/MuteButton.h"
#include "ui/kit/PopupField.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::kit
{

/** A line (or two) of text with its own tooltip, clickable when onClick is
    set: the mixer strips' name, LIM, peak and value. Paints only its text
    (and LIM's dot); repaints only when they change. */
class TextCell final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TextCell (const theme::type::Style&, juce::Justification = juce::Justification::centredLeft, int maxLines = 1);

    void setText (const juce::String&, juce::uint32 colour);
    void setDot (juce::uint32 colour);   // a 5 px dot before the text (LIM); 0: none

    std::function<void()> onClick;
    const juce::String& getText() const noexcept { return text; }
    juce::uint32 getColour() const noexcept { return colour; }

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    theme::type::Style style;
    juce::Justification justification;
    int maxLines;
    juce::String text;
    juce::uint32 colour = 0, dot = 0;
};

/** The highest level since the last reset, as the mixer view shows it over a
    fader: "−8.2" (#C4C4C9; #FF8A80 from −1 dBFS, where the limiter starts),
    "—" while muted or quieter than the meter shows. A click resets it. */
class PeakHold
{
public:
    void feed (float linearPeak) noexcept;
    void reset() noexcept { peakDb = -100.0f; }
    float getDb() const noexcept { return peakDb; }

    juce::String text (bool muted) const;
    juce::uint32 colour (bool muted) const noexcept;

private:
    float peakDb = -100.0f;
};

/** One output in the mixer view (V3.2-CHANGES §3.3), 94 px wide (the first
    82, without its left padding): the name (two lines, its channels in the
    tooltip); LIM (lit amber, flat, while the −1 dBFS safety limiter works,
    held 1 s) and the peak since reset (a click resets it and the output's
    clip count); the dBFS scale, the meter (one bar for a mono output) and
    the level fader (send law, 0 dB line, double-click for 0 dB); under them
    the level (no unit, "−∞" at the bottom, dim while muted) and M.
    The fader drives the output's level: onLevelChange while it moves,
    onLevelCommit when the mouse lets go (the owner saves the routing). */
class MixerStrip final : public juce::Component
{
public:
    MixerStrip();

    void setOutput (const juce::String& name, const juce::String& channels, bool stereo, float levelDb, bool muted);
    void setFirst (bool);   // no left padding, 82 wide

    /** UI timer: the output's peaks (linear) and its limiter's gain reduction (dB, >= 0). */
    void setPeaks (float left, float right);
    void setLimiterReduction (float dB);

    std::function<void (float dB)> onLevelChange;
    std::function<void (float dB)> onLevelCommit;
    std::function<void (bool muted)> onMuteClicked;
    std::function<void()> onPeakReset;

    const juce::String& getName() const noexcept { return name; }
    Fader& getFader() noexcept { return fader; }
    MuteButton& getMuteButton() noexcept { return mute; }
    TextCell& getPeakCell() noexcept { return peak; }
    TextCell& getValueCell() noexcept { return value; }
    bool isLimiting() const noexcept { return limiting; }

    static int widthFor (bool first) noexcept { return first ? theme::size::mixerFirstStripW : theme::size::mixerStripW; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showLevel (float dB);
    void showPeak();

    juce::String name, channels;
    bool muted = false, first = false, limiting = false;
    int limitHold = 0;
    PeakHold hold;
    TextCell nameCell { theme::type::mixerName, juce::Justification::topLeft, 2 };
    TextCell lim { theme::type::limLabel };
    TextCell peak { theme::type::peakValue, juce::Justification::centred };
    TextCell value { theme::type::outputValue, juce::Justification::centredRight };
    DbfsMeter meter;
    Fader fader { Fader::Orientation::mixer };
    MuteButton mute;
    juce::Rectangle<float> scaleColumn;
};

/** The mixer view's input strip (V3.2-CHANGES §3.3): "Input", the compact
    picker (the playing patch's input; the owner shows the menu), its peak
    since reset (a click resets it), the dBFS scale and one meter bar. */
class InputStrip final : public juce::Component
{
public:
    InputStrip();

    void setInput (const juce::String& name, const juce::String& caption, bool missing);
    void setPeak (float linear);

    PopupField& getPicker() noexcept { return picker; }
    TextCell& getPeakCell() noexcept { return peak; }

    static int idealWidth() noexcept { return theme::size::mixerInputCols + 2 * theme::size::mixerPadX; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PopupField picker;
    PeakHold hold;
    TextCell peak { theme::type::peakValue, juce::Justification::centredRight };
    DbfsMeter meter;
    juce::Rectangle<float> labelArea, scaleColumn;
};

} // namespace encore::kit
