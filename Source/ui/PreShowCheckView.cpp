// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PreShowCheckView.h"

namespace encore
{

namespace
{
    using Status = PreShowCheckResult::Status;
    using Fix = PreShowCheckResult::Fix;

    juce::Colour colourFor (Status s)
    {
        return s == Status::ok      ? juce::Colour (0xff2e9e5b)
             : s == Status::warning ? juce::Colour (0xfff0b429)
                                    : juce::Colour (0xffe5484d);
    }

    juce::String fixLabel (Fix f)
    {
        switch (f)
        {
            case Fix::audioSettings:  return "Audio settings...";
            case Fix::midiSettings:   return "MIDI settings...";
            case Fix::save:           return "Save now";
            case Fix::resetCounters:  return "Reset counters";
            case Fix::unmute:         return "Unmute";
            case Fix::none:           break;
        }

        return {};
    }

    constexpr int rowHeight = 54;
}

class PreShowCheckView::Row final : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    explicit Row (PreShowCheckView& o) : owner (o)
    {
        button.setWantsKeyboardFocus (false);
        button.onClick = [this] { if (owner.applyFix != nullptr) owner.applyFix (result.fix); };
        addChildComponent (button);
    }

    void show (const PreShowCheckResult& r)
    {
        if (r.status == result.status && r.title == result.title && r.detail == result.detail && r.fix == result.fix)
            return;

        result = r;
        button.setButtonText (fixLabel (r.fix));
        button.setVisible (r.fix != Fix::none);
        setTooltip (r.detail);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (4, 3);
        g.setColour (juce::Colour (0xff1c2026));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);

        auto light = r.removeFromLeft (36).toFloat();
        g.setColour (colourFor (result.status));
        g.fillEllipse (light.withSizeKeepingCentre (14.0f, 14.0f));

        if (button.isVisible())
            r.removeFromRight (150);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (result.title, r.removeFromTop (r.getHeight() / 2 + 2).reduced (0, 0), juce::Justification::bottomLeft, true);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (result.detail, r, juce::Justification::topLeft, true);
    }

    void resized() override
    {
        button.setBounds (getLocalBounds().reduced (10, 12).removeFromRight (140));
    }

private:
    PreShowCheckView& owner;
    PreShowCheckResult result { Status::ok, "-", "-", Fix::none };
    juce::TextButton button;
};

PreShowCheckView::PreShowCheckView (std::function<PreShowFacts()> g, std::function<void (Fix)> f)
    : gather (std::move (g)), applyFix (std::move (f))
{
    summary.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    addAndMakeVisible (summary);

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setSize (640, 620);
    refresh();
    startTimer (1000);
}

PreShowCheckView::~PreShowCheckView() = default;

void PreShowCheckView::timerCallback() { refresh(); }

void PreShowCheckView::refresh()
{
    results = runPreShowChecks (gather());

    int problems = 0, warnings = 0;

    for (auto& r : results)
    {
        problems += r.status == Status::problem ? 1 : 0;
        warnings += r.status == Status::warning ? 1 : 0;
    }

    summary.setText (problems > 0 ? juce::String (problems) + " problem(s) to fix before the show"
                     : warnings > 0 ? "Ready, with " + juce::String (warnings) + " thing(s) to look at"
                                    : "Ready for the show",
                     juce::dontSendNotification);
    summary.setColour (juce::Label::textColourId, colourFor (problems > 0 ? Status::problem : warnings > 0 ? Status::warning : Status::ok));

    while (rows.size() < (int) results.size())
        rowsHolder.addAndMakeVisible (rows.add (new Row (*this)));

    while (rows.size() > (int) results.size())
        rows.removeLast();

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->show (results[(size_t) i]);

    resized();
}

void PreShowCheckView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121417));
}

void PreShowCheckView::resized()
{
    auto area = getLocalBounds().reduced (12);
    summary.setBounds (area.removeFromTop (36));
    area.removeFromTop (6);
    viewport.setBounds (area);

    const auto width = area.getWidth() - viewport.getScrollBarThickness();
    rowsHolder.setSize (width, rows.size() * rowHeight);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}

} // namespace encore
