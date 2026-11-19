// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** The bottom row's quiet health status (V3.2-CHANGES §3.2): a flat button,
    two lines. Healthy: a 6 px green dot and "Healthy" in #9A9AA1; with
    problems: the amber warn icon and "1 thing to check" / "N things to
    check" in amber semibold. Under it the detail ("CPU 9% · 5.3 ms") in
    #8C8C93. Hover #232326. The tooltip puts the problems first, then what a
    click does; the owner opens the details in onClick. Status is never
    colour alone: the icon and the words say it too. */
class HealthStatus final : public juce::Button
{
public:
    HealthStatus();

    /** problems: the lines to check (empty: healthy); detail: the second line. */
    void setStatus (const juce::StringArray& problems, const juce::String& detail);
    void setBottomAligned (bool);   // the mixer view: the lines sit at the bottom of the column

    int getNumProblems() const noexcept { return numProblems; }
    const juce::String& getHeadline() const noexcept { return headline; }
    const juce::String& getDetail() const noexcept { return detail; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int numProblems = 0;
    juce::String headline { "Healthy" }, detail;
    bool bottomAligned = false;
};

} // namespace encore::kit
