// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/ParameterList.h"

#include "ui/kit/Chip.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/Paint.h"

namespace encore::kit
{

using namespace theme;

//==============================================================================
/** One parameter: its name, a slider, its text. The ListBox reuses rows. */
class ParameterList::Row final : public juce::Component
{
public:
    Row()
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setWantsKeyboardFocus (false);
        slider.setMouseClickGrabsKeyboardFocus (false);
        slider.onDragStart = [this] { if (parameter != nullptr) parameter->beginChangeGesture(); };
        slider.onDragEnd = [this] { if (parameter != nullptr) parameter->endChangeGesture(); };
        slider.onValueChange = [this]
        {
            if (parameter != nullptr && ! juce::approximatelyEqual (parameter->getValue(), (float) slider.getValue()))
            {
                parameter->setValueNotifyingHost ((float) slider.getValue());
                update();
            }
        };
        addAndMakeVisible (slider);
    }

    void show (juce::AudioProcessorParameter* p, int index)
    {
        if (p != parameter)
        {
            parameter = p;
            name = p != nullptr ? p->getName (64) : juce::String();
            slider.setTitle (name);

            if (p != nullptr)
                slider.setDoubleClickReturnValue (true, p->getDefaultValue());
        }

        odd = index % 2 == 1;
        update();
    }

    /** The value and its text, if they changed. */
    void update()
    {
        if (parameter == nullptr)
            return;

        const auto v = parameter->getValue();

        if (! slider.isMouseButtonDown() && ! juce::approximatelyEqual ((float) slider.getValue(), v))
            slider.setValue (v, juce::dontSendNotification);

        const auto t = parameter->getCurrentValueAsText();

        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (odd)
            g.fillAll (paint::c (colour::mappingRowAlt));

        const auto r = getLocalBounds().toFloat();
        paint::text (g, name, r.withX ((float) size::paramListPadX).withWidth ((float) size::paramListNameW), type::body, colour::textSoft);
        paint::text (g, text, r.withTrimmedRight ((float) size::paramListPadX).withLeft (r.getRight() - (float) (size::paramListPadX + size::paramListValueW)),
                     type::body, colour::text, juce::Justification::centredRight);
    }

    void resized() override
    {
        // The slider's track spans its width; the thumb's half hangs over each end.
        const auto half = size::paramListThumb / 2;
        slider.setBounds (size::paramListSliderX - half, 0, size::paramListSliderW + 2 * half, getHeight());
    }

private:
    juce::AudioProcessorParameter* parameter = nullptr;
    juce::Slider slider;
    juce::String name, text;
    bool odd = false;
};

//==============================================================================
class ParameterList::Rows final : public juce::ListBoxModel
{
public:
    explicit Rows (ParameterList& o) : owner (o) {}

    int getNumRows() override { return owner.parameters.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        if (! juce::isPositiveAndBelow (row, owner.parameters.size()))
        {
            delete existing;
            return nullptr;
        }

        auto* r = dynamic_cast<Row*> (existing);

        if (r == nullptr)
        {
            delete existing;
            r = new Row();
        }

        r->show (owner.parameters[row], row);
        return r;
    }

private:
    ParameterList& owner;
};

//==============================================================================
ParameterList::ParameterList (juce::AudioProcessor& processor, const juce::String& format, const juce::String& pluginName)
    : rows (std::make_unique<Rows> (*this))
{
    for (auto* p : processor.getParameters())
        parameters.add (p);

    formatCaps = paint::caps (type::tagChip, format.isNotEmpty() ? format : juce::String ("VST3"));
    name = pluginName;
    nameWidth = textWidth (font (type::paramListName), name);
    note = "has no window of its own: its parameters";
    count = juce::String (parameters.size()) + (parameters.size() == 1 ? " parameter" : " parameters");
    footer = "Changes save with the patch. The window comes back when the patch rebuilds.";

    list.setModel (rows.get());
    list.setRowHeight (size::paramListRowH);
    list.setColour (juce::ListBox::backgroundColourId, paint::c (colour::pane));
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setWantsKeyboardFocus (false);
    list.setTitle (name + " parameters");
    addAndMakeVisible (list);
    setOpaque (true);
    setSize (idealSize().x, idealSize().y);
}

ParameterList::~ParameterList()
{
    list.setModel (nullptr);
}

juce::Point<int> ParameterList::idealSize()
{
    return { size::paramListW, size::paramListHeaderH + size::paramListH + size::paramListFooterH };
}

void ParameterList::refresh()
{
    for (int i = 0; i < parameters.size(); ++i)
        if (auto* row = dynamic_cast<Row*> (list.getComponentForRowNumber (i)))
            row->update();
}

void ParameterList::resized()
{
    list.setBounds (getLocalBounds().withTrimmedTop (size::paramListHeaderH).withTrimmedBottom (size::paramListFooterH));
}

void ParameterList::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::pane));

    // Header: format chip, name, note; the count on the right.
    const auto header = getLocalBounds().withHeight (size::paramListHeaderH).toFloat();
    paint::verticalGradient (g, header, colour::subToolbarTop, colour::subToolbarBot, 0.0f);
    g.setColour (paint::c (colour::paneHeaderLine));
    g.fillRect (header.withTop (header.getBottom() - 1.0f));

    auto x = (float) size::paramListPadX;
    const auto chipW = chipWidth (formatCaps);
    paintChip (g, juce::Rectangle<float> (chipW, (float) size::paramListChipH).withPosition (x, header.getCentreY() - (float) size::paramListChipH * 0.5f),
               formatCaps, ChipStyle::plain);
    x += chipW + (float) size::paramListGap;

    const auto right = header.getRight() - (float) size::paramListPadX;
    const auto countArea = header.withLeft (right - (float) size::paramListCountW).withRight (right);
    paint::text (g, count, countArea, type::label, colour::textMuted, juce::Justification::centredRight);

    const auto nameW = juce::jmin (nameWidth, countArea.getX() - x);
    paint::text (g, name, header.withX (x).withWidth (nameW), type::paramListName, colour::text);
    x += nameW + (float) size::paramListGap;
    paint::text (g, note, header.withX (x).withRight (countArea.getX()), type::paramListNote, colour::textMuted);

    // Footer: a line above, the note.
    const auto foot = getLocalBounds().removeFromBottom (size::paramListFooterH).toFloat();
    g.setColour (paint::c (colour::paneHeaderLine));
    g.fillRect (foot.withHeight (1.0f));
    paint::text (g, footer, foot.withTrimmedLeft ((float) size::paramListPadX), type::caption, colour::textMuted);
}

} // namespace encore::kit
