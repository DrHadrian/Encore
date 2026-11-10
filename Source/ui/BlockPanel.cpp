// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/BlockPanel.h"

namespace encore
{

namespace
{
    constexpr int controlWidth = 80;
    constexpr int padding = 12;
    constexpr int headerHeight = 32;
    constexpr int fileRowHeight = 50;
    constexpr int controlsHeight = 118;
    constexpr int statusHeight = 22;

    const auto panelColour = juce::Colour (0xff1b1e23);
    const auto onColour    = juce::Colour (0xff2e9e5b);
    const auto offColour   = juce::Colour (0xff3a3f47);
    const auto accent      = juce::Colour (0xff4f9dff);

    bool isOnOffParameter (const Parameter& p)
    {
        const auto& r = p.getSpec().range;
        return juce::approximatelyEqual (r.start, 0.0f) && juce::approximatelyEqual (r.end, 1.0f)
            && juce::approximatelyEqual (r.interval, 1.0f);
    }
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

    if (auto* slot = block.getFileSlot())
    {
        loadButton.setButtonText (slot->buttonText);
        loadButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible (loadButton);

        fileName.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        fileName.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (fileName);

        fileInfo.setFont (juce::FontOptions (13.0f));
        fileInfo.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (fileInfo);
    }

    for (auto& p : block.getParameters())
    {
        auto control = std::make_unique<Control> (*p);
        const auto& spec = p->getSpec();
        auto* param = p.get();

        control->label.setText (spec.name, juce::dontSendNotification);
        control->label.setFont (juce::FontOptions (14.0f));
        control->label.setJustificationType (juce::Justification::centred);
        control->label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        addAndMakeVisible (control->label);

        if (isOnOffParameter (*p))
        {
            // On/off parameters (level match, detect guitar) are switches, not knobs.
            control->isSwitch = true;
            auto& b = control->switchButton;
            b.setClickingTogglesState (true);
            b.setToggleState (p->getValue() >= 0.5f, juce::dontSendNotification);
            b.setButtonText (b.getToggleState() ? "ON" : "OFF");
            b.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.8f));
            b.setColour (juce::TextButton::buttonColourId, offColour);
            b.onClick = [param, &b]
            {
                param->setValue (b.getToggleState() ? 1.0f : 0.0f);
                b.setButtonText (b.getToggleState() ? "ON" : "OFF");
            };
            addAndMakeVisible (b);
        }
        else
        {
            const auto& r = spec.range;
            auto& s = control->slider;
            s.setNormalisableRange ({ (double) r.start, (double) r.end, (double) r.interval,
                                      (double) r.skew, r.symmetricSkew });
            s.setValue (p->getValue(), juce::dontSendNotification);
            s.setDoubleClickReturnValue (true, spec.defaultValue);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, controlWidth - 6, 20);
            s.setColour (juce::Slider::rotarySliderFillColourId, accent);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.textFromValueFunction = [param] (double v) { return param->toText ((float) v); };
            s.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
            s.onValueChange = [param, &s] { param->setValue ((float) s.getValue()); };
            s.updateText();
            addAndMakeVisible (s);
        }

        controls.push_back (std::move (control));
    }

    refresh();
}

BlockPanel::~BlockPanel() = default;

void BlockPanel::setStatusLight (juce::String label, std::function<bool()> isLit)
{
    statusLabel = std::move (label);
    statusLit = std::move (isLit);
    resized();
    repaint();
}

//==============================================================================
void BlockPanel::chooseFile()
{
    auto* slot = block.getFileSlot();

    if (slot == nullptr || loading)
        return;

    const auto start = getBrowseFolder != nullptr ? getBrowseFolder() : juce::File();
    chooser = std::make_unique<juce::FileChooser> (slot->buttonText.trimCharactersAtEnd ("."), start, slot->wildcard);

    juce::WeakReference<BlockPanel> weakThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [weakThis] (const juce::FileChooser& fc)
                          {
                              if (auto* self = weakThis.get())
                                  if (fc.getResult().existsAsFile())
                                      self->loadFile (fc.getResult());
                          });
}

void BlockPanel::loadFile (const juce::File& file)
{
    setLoading (true);

    juce::WeakReference<BlockPanel> weakThis (this);
    block.loadFileAsync (file, [weakThis, file] (juce::String error)
    {
        auto* self = weakThis.get();

        if (self == nullptr)
            return;

        self->setLoading (false);

        if (error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Couldn't load " + file.getFileName(), error);
            return;
        }

        if (self->onFileLoaded != nullptr)
            self->onFileLoaded (file);
    });
}

void BlockPanel::setLoading (bool isLoading)
{
    loading = isLoading;
    loadButton.setEnabled (! isLoading);
    shownFile.clear();   // force the file row to redraw
    refresh();
}

//==============================================================================
int BlockPanel::getPreferredWidth() const
{
    const auto minimum = block.getFileSlot() != nullptr ? 360 : 200;
    return juce::jmax (minimum, (int) controls.size() * controlWidth + 2 * padding);
}

int BlockPanel::getPreferredHeight() const
{
    return padding + headerHeight
         + (block.getFileSlot() != nullptr ? fileRowHeight : 0)
         + controlsHeight
         + (statusLit != nullptr ? statusHeight : 0)
         + padding;
}

void BlockPanel::refresh()
{
    for (auto& c : controls)
    {
        const auto value = c->parameter.getValue();

        if (c->isSwitch)
        {
            const auto on = value >= 0.5f;

            if (c->switchButton.getToggleState() != on)
            {
                c->switchButton.setToggleState (on, juce::dontSendNotification);
                c->switchButton.setButtonText (on ? "ON" : "OFF");
            }
        }
        else if (! c->slider.isMouseButtonDown()
                 && ! juce::approximatelyEqual ((float) c->slider.getValue(), value))
        {
            c->slider.setValue (value, juce::dontSendNotification);
        }
    }

    if (power.getToggleState() != block.isEnabled())
    {
        power.setToggleState (block.isEnabled(), juce::dontSendNotification);
        power.setButtonText (block.isEnabled() ? "ON" : "OFF");
    }

    cpu.setText (block.isEnabled() ? juce::String (block.getCpuLoad() * 100.0, 1) + " % CPU" : juce::String(),
                 juce::dontSendNotification);

    if (block.getFileSlot() != nullptr)
    {
        const auto file = block.getLoadedFile();
        const auto name = loading ? juce::String ("Loading...")
                                  : (file == juce::File() ? juce::String ("(none)") : file.getFileNameWithoutExtension());
        const auto info = loading ? juce::String() : block.getLoadedFileInfo();

        if (name != shownFile || info != shownInfo)
        {
            shownFile = name;
            shownInfo = info;
            fileName.setText (name, juce::dontSendNotification);
            fileName.setTooltip (file.getFullPathName());
            fileInfo.setText (info, juce::dontSendNotification);
        }
    }

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

//==============================================================================
void BlockPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (block.isEnabled() ? onColour : offColour);
    g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 2.0f);

    if (statusLit != nullptr)
    {
        auto area = getLocalBounds().reduced (padding, 0).removeFromBottom (statusHeight + padding).removeFromTop (statusHeight);
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

    auto header = area.removeFromTop (headerHeight);
    power.setBounds (header.removeFromLeft (52).reduced (0, 2));
    header.removeFromLeft (8);
    cpu.setBounds (header.removeFromRight (90));
    title.setBounds (header);

    if (block.getFileSlot() != nullptr)
    {
        auto row = area.removeFromTop (fileRowHeight).reduced (0, 6);
        loadButton.setBounds (row.removeFromRight (110).reduced (0, 4));
        row.removeFromRight (8);
        fileName.setBounds (row.removeFromTop (row.getHeight() / 2));
        fileInfo.setBounds (row);
    }

    if (statusLit != nullptr)
        area.removeFromBottom (statusHeight);

    for (auto& c : controls)
    {
        auto column = area.removeFromLeft (controlWidth);
        c->label.setBounds (column.removeFromTop (20));

        if (c->isSwitch)
            c->switchButton.setBounds (column.withSizeKeepingCentre (controlWidth - 16, 32));
        else
            c->slider.setBounds (column);
    }
}

} // namespace encore
