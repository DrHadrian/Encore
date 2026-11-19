// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PreShowCheckView.h"

#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    using Status = PreShowCheckResult::Status;
    using Fix = PreShowCheckResult::Fix;

    juce::String fixLabel (Fix f)
    {
        switch (f)
        {
            case Fix::audioSettings:  return "Audio settings...";
            case Fix::midiSettings:   return "MIDI settings...";
            case Fix::pluginSettings: return "Plugin settings...";
            case Fix::save:           return "Save now";
            case Fix::resetCounters:  return "Reset counters";
            case Fix::unmute:         return "Unmute";
            case Fix::none:           break;
        }

        return {};
    }

    kit::Status kitStatus (Status s)
    {
        return s == Status::ok ? kit::Status::ok : s == Status::warning ? kit::Status::warning : kit::Status::problem;
    }

    juce::uint32 headingColour (Status s)
    {
        return s == Status::ok ? colour::okBadgeTick : s == Status::warning ? colour::amber : colour::danger;
    }

    const juce::String& middot()
    {
        static const juce::String s (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        return s;
    }
}

//==============================================================================
/** One check: badge, title (red for a problem), detail, the fix button. */
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
            return;   // unchanged: nothing to do

        result = r;
        button.setButtonText (fixLabel (r.fix));
        setVariant (button, r.status == Status::problem ? Variant::primary : Variant::standard);
        button.setVisible (r.fix != Fix::none);
        setTooltip (r.detail);
        resized();
        repaint();
    }

    Status getStatus() const noexcept { return result.status; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        if (result.status == Status::problem)
            g.fillAll (paint::c (colour::problemRowTint));

        g.setColour (paint::c (colour::checkRowLine));
        paint::hline (g, r.getX(), r.getBottom() - 1.0f, r.getWidth());

        kit::paintStatusBadge (g, juce::Rectangle<float> ((float) size::checkPadX, r.getCentreY() - (float) size::rowBadge * 0.5f,
                                                          (float) size::rowBadge, (float) size::rowBadge),
                               kitStatus (result.status));

        const auto titleInk = result.status == Status::problem ? colour::danger : colour::text;

        if (result.detail.isEmpty())
        {
            paint::text (g, result.title, textBox, type::checkTitle, titleInk);
            return;
        }

        const auto top = textBox.getCentreY() - (float) size::rowTitleH;
        paint::text (g, result.title, textBox.withY (top).withHeight ((float) size::rowTitleH), type::checkTitle, titleInk);
        paint::text (g, result.detail, textBox.withY (top + (float) (size::rowTitleH + size::rowLineGap)).withHeight ((float) size::rowTitleH),
                     type::checkDetail, colour::textSecondary);
    }

    void resized() override
    {
        const auto buttonH = size::buttonHeight + size::buttonDrop;
        const auto w = button.getBestWidthForHeight (buttonH);
        button.setBounds (getWidth() - size::checkPadX - w, (getHeight() - size::buttonHeight) / 2, w, buttonH);
        const auto right = button.isVisible() ? button.getX() - size::checkPadX : getWidth() - size::checkPadX;
        textBox = juce::Rectangle<int> (size::rowTextX, 0, juce::jmax (0, right - size::rowTextX), getHeight()).toFloat();
    }

private:
    PreShowCheckView& owner;
    PreShowCheckResult result { Status::ok, "-", "-", Fix::none };
    juce::TextButton button;
    juce::Rectangle<float> textBox;
};

//==============================================================================
/** The lines with their group headings. */
class PreShowCheckView::List final : public juce::Component
{
public:
    std::vector<std::pair<juce::Rectangle<float>, Status>> headings;

    void paint (juce::Graphics& g) override
    {
        static const juce::String names[] = { paint::caps (type::checkHeading, "Fine"), paint::caps (type::checkHeading, "To look at"),
                                              paint::caps (type::checkHeading, "Problems") };

        for (auto& [box, status] : headings)
            paint::text (g, names[(int) status], box.withTrimmedLeft ((float) size::checkPadX), type::checkHeading, headingColour (status));
    }
};

//==============================================================================
PreShowCheckView::PreShowCheckView (std::function<PreShowFacts()> g, std::function<void (Fix)> f)
    : gather (std::move (g)), applyFix (std::move (f))
{
    addAndMakeVisible (badge);
    list = std::make_unique<List>();
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setSize (size::preShowW, size::preShowH - size::dialogTitleBar - size::dialogFooter);
    refresh();
    startTimer (1000);
}

PreShowCheckView::~PreShowCheckView() = default;

void PreShowCheckView::timerCallback() { refresh(); }

void PreShowCheckView::refresh()
{
    results = runPreShowChecks (gather());

    int counts[3] = {};

    for (auto& r : results)
        ++counts[(int) r.status];

    const auto problems = counts[(int) Status::problem], warnings = counts[(int) Status::warning], fine = counts[(int) Status::ok];
    const auto newSummary = problems > 0 ? juce::String (problems) + " problem(s) to fix before the show"
                          : warnings > 0 ? "Ready, with " + juce::String (warnings) + " thing(s) to look at"
                                         : juce::String ("Ready for the show");
    const auto newSub = juce::String (problems) + (problems == 1 ? " problem" : " problems") + middot() + juce::String (warnings) + " to look at"
                        + middot() + juce::String (fine) + " fine" + middot()
                        + "checks again every second, so you can fix things and watch them turn green";
    const auto newWorst = problems > 0 ? Status::problem : warnings > 0 ? Status::warning : Status::ok;

    if (newSummary != summary || newSub != sub || newWorst != worst)
    {
        summary = newSummary;
        sub = newSub;
        worst = newWorst;
        badge.setStatus (kitStatus (worst));
        repaint (0, 0, getWidth(), size::summaryBandH);
    }

    // Lines update in place; only a change in their number makes or removes rows.
    auto changed = rows.size() != (int) results.size();

    while (rows.size() < (int) results.size())
        list->addAndMakeVisible (rows.add (new Row (*this)));

    while (rows.size() > (int) results.size())
        rows.removeLast();

    for (int i = 0; i < rows.size(); ++i)
    {
        changed = changed || rows[i]->getStatus() != results[(size_t) i].status;
        rows[i]->show (results[(size_t) i]);
    }

    if (changed)
        resized();
}

void PreShowCheckView::paint (juce::Graphics& g)
{
    // The summary band, tinted by the worst status.
    g.setGradientFill (bandFill);
    g.fillRect (0, 0, getWidth(), size::summaryBandH);

    static const juce::String live ("Live"), key ("F9");
    const auto x = (float) size::summaryTextX;
    const auto right = (float) (getWidth() - size::checkPadX);
    paint::text (g, summary, { x, (float) size::summaryTitleY, right - x - (float) size::liveAreaW, (float) size::summaryTitleH }, type::summaryLarge, colour::text);
    paint::text (g, sub, { x, (float) size::summarySubY, right - x - (float) size::liveAreaW, (float) size::summarySubH }, type::checkDetail, colour::textSecondary);

    // "● Live" and the key, top right.
    const auto liveW = textWidth (font (type::caption), live);
    const juce::Rectangle<float> liveBox (right - liveW, (float) (size::summaryTitleY + size::liveTop), liveW, (float) size::summarySubH);
    paint::text (g, live, liveBox, type::caption, colour::textMuted, juce::Justification::centredRight);
    g.setColour (paint::c (colour::healthy));
    g.fillEllipse (juce::Rectangle<float> ((float) size::liveDot, (float) size::liveDot)
                       .withCentre ({ liveBox.getX() - (float) size::liveGap, liveBox.getCentreY() }));
    paint::text (g, key, liveBox.withY (liveBox.getBottom() + (float) size::liveKeyGap), type::caption, colour::textMuted, juce::Justification::centredRight);
}

void PreShowCheckView::resized()
{
    const auto bandH = size::summaryBandH;
    const auto badgeSize = kit::SummaryBadge::getIdealSize();
    badge.setBounds (size::summaryBadgeX - (badgeSize - size::summaryBadge) / 2, size::summaryBadgeY - (badgeSize - size::summaryBadge) / 2,
                     badgeSize, badgeSize);

    const auto top = worst == Status::problem ? colour::problemBandTop : worst == Status::warning ? colour::warningBandTop : colour::okBandTop;
    const auto bot = worst == Status::problem ? colour::problemBandBot : worst == Status::warning ? colour::warningBandBot : colour::okBandBot;
    bandFill = juce::ColourGradient::vertical (paint::c (top), 0.0f, paint::c (bot), (float) bandH);

    viewport.setBounds (getLocalBounds().withTrimmedTop (bandH));

    // Headings where the status changes, then the lines.
    const auto width = viewport.getWidth() - (viewport.canScrollVertically() ? viewport.getScrollBarThickness() : 0);
    list->headings.clear();
    auto y = size::checkListTop;

    for (int i = 0; i < rows.size(); ++i)
    {
        if (i == 0 || rows[i]->getStatus() != rows[i - 1]->getStatus())
        {
            list->headings.push_back ({ juce::Rectangle<int> (0, y, width, size::checkHeadingH).toFloat(), rows[i]->getStatus() });
            y += size::checkHeadingH;
        }

        rows[i]->setBounds (0, y, width, size::preShowRowH);
        y += size::preShowRowH;
    }

    list->setSize (width, y);
    list->repaint();
}

//==============================================================================
PreShowCheckDialog::PreShowCheckDialog (std::function<PreShowFacts()> gather, std::function<void (PreShowCheckResult::Fix)> fix,
                                        std::function<void()> close)
    : frame ("Pre-show check", "Close pre-show check"), view (std::move (gather), std::move (fix))
{
    frame.setContent (&view);
    frame.setFooterText (juce::String (juce::CharPointer_UTF8 ("Worst first. Lines with a button can be fixed from here. "
                                                               "The log is in Show \xe2\x86\x92 Open log folder.")));
    frame.onClose = std::move (close);
    overlay = std::make_unique<kit::DialogOverlay> (frame, juce::Rectangle<int> (size::preShowW + 2, size::preShowH + 2));
}

} // namespace encore
