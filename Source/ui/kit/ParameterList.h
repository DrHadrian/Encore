// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace encore::kit
{

/** A plugin's parameters, for a plugin without a window of its own
    (NEW-FEATURES-M10-M11 §A.4), in place of JUCE's generic editor: a header
    (format chip, name, "has no window of its own: its parameters", the
    count), a ListBox of 33 px rows (name, a horizontal slider, the plugin's
    own text for the value; every other row darker) and a footer line.

    Dragging a slider sets the parameter as the generic editor did (change
    gestures, setValueNotifyingHost). Changes made elsewhere (the plugin, a
    pedal) show when the owner calls refresh() from its UI timer: no timer
    of its own. Message thread only. */
class ParameterList final : public juce::Component
{
public:
    ParameterList (juce::AudioProcessor&, const juce::String& format, const juce::String& name);
    ~ParameterList() override;

    /** Re-reads the visible rows' values and texts (cheap when nothing changed). */
    void refresh();

    /** 580 wide; the list 455 high (it scrolls past 13 rows). */
    static juce::Point<int> idealSize();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    class Rows;

    juce::Array<juce::AudioProcessorParameter*> parameters;
    juce::String formatCaps, name, note, count, footer;
    float nameWidth = 0.0f;
    std::unique_ptr<Rows> rows;
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterList)
};

} // namespace encore::kit
