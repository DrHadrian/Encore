// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BlockPanel.h"

namespace encore
{

namespace
{
    constexpr int knobWidth = 84;
    constexpr int padding = 12;
    constexpr int headerHeight = 40;

    const auto panelColour = juce::Colour (0xff1b1e23);
    const auto onColour    = juce::Colour (0xff2e9e5b);
    const auto offColour   = juce::Colour (0xff3a3f47);
    const auto accent      = juce::Colour (0xff4f9dff);
}

BlockPanel::BlockPanel (Block& blockToEdit) : block (blockToEdit)
{
    title.setText (block.getName(), juce::dontSendNotification);
    title.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (title);

    cpu.setFont (juce::FontOptions (13.0f));
    cpu.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    cpu.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpu);

    power.setClickingTogglesState (true);
    power.setToggleState (block.isEnabled(), juce::dontSendNotification);
    power.setColour (juce::TextButton::buttonOnColourId, onColour);
    power.setColour (juce::TextButton::buttonColourId, offColour);
    power.onClick = [this]
    {
        block.setEnabled (power.getToggleState());
        power.setButtonText (power.getToggleState() ? "ON" : "OFF");
        repaint();
    };
    addAndMakeVisible (power);

    for (auto& p : block.getParameters())
    {
        auto knob = std::make_unique<Knob> (*p);
        const auto& spec = p->getSpec();
        const auto& r = spec.range;

        auto& s = knob->slider;
        s.setNormalisableRange ({ (double) r.start, (double) r.end, (double) r.interval,
                                  (double) r.skew, r.symmetricSkew });
        s.setValue (p->getValue(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, spec.defaultValue);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 8, 20);
        s.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        auto* param = p.get();
        s.textFromValueFunction = [param] (double v) { return param->toText ((float) v); };
        s.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        s.onValueChange = [param, &s] { param->setValue ((float) s.getValue()); };
        s.updateText();
        addAndMakeVisible (s);

        knob->label.setText (spec.name, juce::dontSendNotification);
        knob->label.setFont (juce::FontOptions (14.0f));
        knob->label.setJustificationType (juce::Justification::centred);
        knob->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        addAndMakeVisible (knob->label);

        knobs.push_back (std::move (knob));
    }
}

BlockPanel::~BlockPanel() = default;

void BlockPanel::setStatusLight (juce::String label, std::function<bool()> isLit)
{
    statusLabel = std::move (label);
    statusLit = std::move (isLit);
    repaint();
}

int BlockPanel::getPreferredWidth() const
{
    return juce::jmax (220, (int) knobs.size() * knobWidth + 2 * padding);
}

void BlockPanel::refresh()
{
    for (auto& k : knobs)
        if (! k->slider.isMouseButtonDown()
            && ! juce::approximatelyEqual ((float) k->slider.getValue(), k->parameter.getValue()))
            k->slider.setValue (k->parameter.getValue(), juce::dontSendNotification);

    if (power.getToggleState() != block.isEnabled())
    {
        power.setToggleState (block.isEnabled(), juce::dontSendNotification);
        power.setButtonText (block.isEnabled() ? "ON" : "OFF");
    }

    cpu.setText (block.isEnabled() ? juce::String (block.getCpuLoad() * 100.0, 1) + " % CPU" : juce::String(),
                 juce::dontSendNotification);

    if (statusLit != nullptr)
    {
        const auto lit = block.isEnabled() && statusLit();

        if (lit != statusWasLit)
        {
            statusWasLit = lit;
            repaint();
        }
    }
}

void BlockPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (block.isEnabled() ? onColour : offColour);
    g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 2.0f);

    if (statusLit != nullptr)
    {
        auto area = getLocalBounds().removeFromBottom (24).reduced (padding, 4);
        const auto light = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (statusWasLit ? onColour : offColour);
        g.fillEllipse (light);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (statusLabel, area.withTrimmedLeft (6), juce::Justification::centredLeft);
    }
}

void BlockPanel::resized()
{
    auto area = getLocalBounds().reduced (padding);

    auto header = area.removeFromTop (headerHeight - padding + 8);
    power.setBounds (header.removeFromLeft (52).reduced (0, 2));
    header.removeFromLeft (8);
    cpu.setBounds (header.removeFromRight (90));
    title.setBounds (header);

    if (statusLit != nullptr)
        area.removeFromBottom (20);

    area.removeFromTop (8);

    for (auto& k : knobs)
    {
        auto column = area.removeFromLeft (knobWidth);
        k->label.setBounds (column.removeFromTop (20));
        k->slider.setBounds (column);
    }
}

} // namespace encore
