// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/PreShowCheck.h"
#include "ui/kit/DialogFrame.h"
#include "ui/kit/StatusBadge.h"

namespace encore
{

/** The pre-show check (SCREENS.md §8): a summary band, then one line per
    check under PROBLEMS / TO LOOK AT / FINE, worst first, with a button where
    there's something to do about it. Re-checks every second while open, so
    you can fix things and watch them turn green; lines update in place. */
class PreShowCheckView final : public juce::Component,
                               private juce::Timer
{
public:
    PreShowCheckView (std::function<PreShowFacts()> gather, std::function<void (const PreShowCheckResult&)> fix);
    ~PreShowCheckView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    class List;

    void timerCallback() override;
    void refresh();

    std::function<PreShowFacts()> gather;
    std::function<void (const PreShowCheckResult&)> applyFix;

    std::vector<PreShowCheckResult> results;
    kit::SummaryBadge badge;
    juce::String summary, sub;
    PreShowCheckResult::Status worst = PreShowCheckResult::Status::ok;
    juce::ColourGradient bandFill;
    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::OwnedArray<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreShowCheckView)
};

/** The check in its dialog over the main window (F9, Show → Pre-show check...). */
class PreShowCheckDialog final
{
public:
    PreShowCheckDialog (std::function<PreShowFacts()> gather, std::function<void (const PreShowCheckResult&)> fix,
                        std::function<void()> close);

    juce::Component& getOverlay() noexcept { return *overlay; }

private:
    kit::DialogFrame frame;
    PreShowCheckView view;
    std::unique_ptr<kit::DialogOverlay> overlay;
};

} // namespace encore
