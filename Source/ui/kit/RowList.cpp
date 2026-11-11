// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/RowList.h"

#include "ui/theme/EncoreTheme.h"

namespace encore::kit
{

using namespace theme;

class RowList::Content final : public juce::Component,
                               public juce::TooltipClient
{
public:
    explicit Content (RowList& o) : owner (o)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto clip = g.getClipBounds();

        for (int row = 0; row + 1 < (int) owner.tops.size(); ++row)
        {
            const auto r = owner.getRowBounds (row);

            if (r.getBottom() < clip.getY())
                continue;

            if (r.getY() > clip.getBottom())
                break;

            owner.model.paintRow (g, row, r);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const auto row = owner.getRowAt (e.getPosition()); row >= 0)
            owner.model.rowClicked (row, e);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto row = owner.getRowAt (e.getPosition()); row >= 0)
            owner.model.rowDoubleClicked (row, e);
    }

    juce::String getTooltip() override { return owner.getTooltip(); }

private:
    RowList& owner;
};

RowList::RowList (Model& m) : model (m), content (std::make_unique<Content> (*this))
{
    setWantsKeyboardFocus (false);
    viewport.setWantsKeyboardFocus (false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (size::scrollbar);
    viewport.setViewedComponent (content.get(), false);
    addAndMakeVisible (viewport);
    updateContent();
}

RowList::~RowList() = default;

void RowList::updateContent()
{
    tops.clear();
    auto y = size::listPad;
    const auto n = juce::jmax (0, model.getNumRows());

    for (int row = 0; row < n; ++row)
    {
        tops.push_back (y);
        y += juce::jmax (0, model.getRowHeight (row)) + (row + 1 < n ? size::listGap : 0);
    }

    tops.push_back (y + size::listPad);
    resized();
    content->repaint();
}

juce::Rectangle<int> RowList::getRowBounds (int row) const
{
    if (! juce::isPositiveAndBelow (row, (int) tops.size() - 1))
        return {};

    const auto next = row + 2 < (int) tops.size() ? tops[(size_t) row + 1] - size::listGap : tops.back() - size::listPad;
    return { size::listPad, tops[(size_t) row], juce::jmax (0, content->getWidth() - 2 * size::listPad), next - tops[(size_t) row] };
}

int RowList::getRowAt (juce::Point<int> p) const
{
    for (int row = 0; row + 1 < (int) tops.size(); ++row)
        if (getRowBounds (row).withX (0).withWidth (content->getWidth()).contains (p))
            return row;

    return -1;
}

void RowList::repaintRow (int row)
{
    content->repaint (getRowBounds (row));
}

void RowList::scrollToEnsureRowIsOnscreen (int row)
{
    const auto r = getRowBounds (row);

    if (r.isEmpty())
        return;

    const auto top = viewport.getViewPositionY(), h = viewport.getViewHeight();

    if (r.getY() < top)
        viewport.setViewPosition (0, r.getY() - size::listPad);
    else if (r.getBottom() > top + h)
        viewport.setViewPosition (0, r.getBottom() + size::listPad - h);
}

void RowList::resized()
{
    viewport.setBounds (getLocalBounds());
    const auto contentH = tops.empty() ? 0 : tops.back();
    const auto needsBar = contentH > getHeight();
    content->setSize (juce::jmax (0, getWidth() - (needsBar ? viewport.getScrollBarThickness() : 0)), contentH);
}

} // namespace encore::kit
