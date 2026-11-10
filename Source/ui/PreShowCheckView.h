// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/PreShowCheck.h"

namespace encore
{

/** The pre-show check: one line per check, red/amber/green, worst first, with
    a button where there's something to do about it. Re-checks every second
    while open, so you can fix things and watch them turn green. */
class PreShowCheckView final : public juce::Component,
                               private juce::Timer
{
public:
    PreShowCheckView (std::function<PreShowFacts()> gather, std::function<void (PreShowCheckResult::Fix)> fix);
    ~PreShowCheckView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    void timerCallback() override;
    void refresh();

    std::function<PreShowFacts()> gather;
    std::function<void (PreShowCheckResult::Fix)> applyFix;

    std::vector<PreShowCheckResult> results;
    juce::Label summary;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreShowCheckView)
};

} // namespace encore
