// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit
{

/** A scrolling list of rows of different heights (pane lists: section
    headings 24, rows 26), 4 px padding and 1 px between rows. The model
    paints the rows and takes the clicks; the list never takes keyboard
    focus (Space and the arrows always step the show). */
class RowList final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    struct Model
    {
        virtual ~Model() = default;
        virtual int getNumRows() = 0;
        virtual int getRowHeight (int row) = 0;
        virtual void paintRow (juce::Graphics&, int row, juce::Rectangle<int> bounds) = 0;
        virtual void rowClicked (int row, const juce::MouseEvent&) = 0;
        virtual void rowDoubleClicked (int row, const juce::MouseEvent&) = 0;
    };

    explicit RowList (Model&);
    ~RowList() override;

    /** Re-reads the number and heights of rows, and repaints. */
    void updateContent();

    void repaintRow (int row);
    void scrollToEnsureRowIsOnscreen (int row);
    juce::Rectangle<int> getRowBounds (int row) const;   // in the content's coordinates
    int getRowAt (juce::Point<int> positionInList) const;

    void resized() override;

private:
    class Content;

    Model& model;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::vector<int> tops;   // row tops; one more entry: the content's height
};

} // namespace encore::kit
