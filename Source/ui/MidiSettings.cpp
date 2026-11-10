// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/MidiSettings.h"

namespace encore
{

namespace
{
    const auto panelColour = juce::Colour (0xff1c2026);
    const auto learnColour = juce::Colour (0xfff0b429);

    void style (juce::Label& label, float size, float alpha = 0.85f, bool bold = false)
    {
        label.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (alpha));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    constexpr int learnTimeoutMs = 15000;

    /** Every action the menu offers, as (action, index) pairs with menu ids. */
    void addActionItems (juce::PopupMenu& menu, int idBase)
    {
        const auto id = [idBase] (MidiAction a, int index) { return idBase + (int) a * 16 + index; };

        menu.addItem (id (MidiAction::nextCue, 1), describe (MidiAction::nextCue, 1));
        menu.addItem (id (MidiAction::previousCue, 1), describe (MidiAction::previousCue, 1));
        menu.addItem (id (MidiAction::programRecall, 1), describe (MidiAction::programRecall, 1));

        juce::PopupMenu stomps, pedals;

        for (int i = 1; i <= maxStomps; ++i)
            stomps.addItem (id (MidiAction::stomp, i), describe (MidiAction::stomp, i));

        for (int i = 1; i <= maxExpressionPedals; ++i)
            pedals.addItem (id (MidiAction::expression, i), describe (MidiAction::expression, i));

        menu.addSubMenu ("Stomp switch", stomps);
        menu.addSubMenu ("Expression pedal", pedals);
        menu.addItem (id (MidiAction::mute, 1), describe (MidiAction::mute, 1));
        menu.addItem (id (MidiAction::tuner, 1), describe (MidiAction::tuner, 1) + " (from milestone 7)");
        menu.addItem (id (MidiAction::panic, 1), describe (MidiAction::panic, 1) + " (from milestone 7)");
    }

    bool decodeAction (int result, int idBase, MidiAction& action, int& index)
    {
        if (result < idBase)
            return false;

        action = (MidiAction) ((result - idBase) / 16);
        index = (result - idBase) % 16;
        return true;
    }
}

//==============================================================================
/** One mapping: [what it does] [which control] [Learn] [x] */
class MidiSettings::Row final : public juce::Component
{
public:
    Row (MidiSettings& o, int i) : owner (o), rowIndex (i)
    {
        actionButton.onClick = [this] { chooseAction(); };
        actionButton.setTooltip ("What this control does (click to change)");
        addAndMakeVisible (actionButton);

        style (triggerLabel, 14.0f);
        addAndMakeVisible (triggerLabel);

        learnButton.setTooltip ("Click, then press the footswitch (or move the pedal)");
        // Row buttons act after their click returns: the action rebuilds the
        // rows, and a button mustn't be deleted from inside its own onClick.
        learnButton.onClick = [this] { later ([] (MidiSettings& settings, int row) { settings.startLearning (row); }); };
        addAndMakeVisible (learnButton);

        removeButton.setTooltip ("Remove this mapping");
        removeButton.onClick = [this]
        {
            later ([] (MidiSettings& settings, int row)
            {
                auto bindings = settings.currentBindings();

                if (juce::isPositiveAndBelow (row, (int) bindings.size()))
                {
                    bindings.erase (bindings.begin() + row);
                    settings.learningRow = -1;
                    settings.router.setLearning (false);
                    settings.storeBindings (bindings);
                }
            });
        };
        addAndMakeVisible (removeButton);
    }

    void show (const MidiBinding& binding, bool learning)
    {
        actionButton.setButtonText (describe (binding.action, binding.index));
        triggerLabel.setText (learning ? juce::String ("Press the control now...") : describe (binding.trigger),
                              juce::dontSendNotification);
        triggerLabel.setColour (juce::Label::textColourId,
                                learning ? learnColour : juce::Colours::white.withAlpha (0.85f));
        learnButton.setButtonText (learning ? "Cancel" : "Learn");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        actionButton.setBounds (r.removeFromLeft (200));
        r.removeFromLeft (10);
        removeButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (6);
        learnButton.setBounds (r.removeFromRight (80));
        r.removeFromRight (10);
        triggerLabel.setBounds (r);
    }

private:
    void later (std::function<void (MidiSettings&, int)> action)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiSettings> (&owner),
                                          row = rowIndex, action = std::move (action)]
        {
            if (safe != nullptr)
                action (*safe, row);
        });
    }

    void chooseAction()
    {
        constexpr int idBase = 1000;
        juce::PopupMenu menu;
        addActionItems (menu, idBase);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&actionButton),
                            [safe = juce::Component::SafePointer<MidiSettings> (&owner), row = rowIndex] (int result)
        {
            MidiAction action;
            int index = 1;

            if (safe == nullptr || ! decodeAction (result, idBase, action, index))
                return;

            auto bindings = safe->currentBindings();

            if (juce::isPositiveAndBelow (row, (int) bindings.size()))
            {
                bindings[(size_t) row].action = action;
                bindings[(size_t) row].index = index;

                if (action == MidiAction::programRecall)
                    bindings[(size_t) row].trigger.kind = MidiTrigger::Kind::programChange;

                safe->storeBindings (bindings);
            }
        });
    }

    MidiSettings& owner;
    const int rowIndex;
    juce::TextButton actionButton, learnButton { "Learn" }, removeButton { "x" };
    juce::Label triggerLabel;
};

//==============================================================================
MidiSettings::MidiSettings (juce::AudioDeviceManager& dm, MidiRouter& r, AppSettings& s, ShowSession& ss,
                            std::function<void()> bindingsChanged, std::function<void()> outputChanged)
    : deviceManager (dm), router (r), settings (s), session (ss),
      applyBindings (std::move (bindingsChanged)), applyOutput (std::move (outputChanged))
{
    for (auto* label : { &inputsLabel, &outputLabel, &mappingsLabel })
    {
        style (*label, 16.0f, 1.0f, true);
        addAndMakeVisible (label);
    }

    style (lastLabel, 14.0f, 0.65f);
    addAndMakeVisible (lastLabel);

    outputChoice.onChange = [this]
    {
        const auto index = outputChoice.getSelectedId() - 2;   // 1 = none
        settings.setMidiOutput (juce::isPositiveAndBelow (index, shownOutputs.size())
                                    ? shownOutputs[index].identifier : juce::String());
        applyOutput();
    };
    outputChoice.setTooltip ("Program and control changes each cue sends (to other gear, or your controller's LEDs)");
    addAndMakeVisible (outputChoice);

    showOwnMappings.setTooltip ("On: the mappings below travel with the show folder. "
                                "Off: this computer's mappings are used for every show.");
    showOwnMappings.onClick = [this]
    {
        stopLearning();
        session.setOwnMidiMappings (showOwnMappings.getToggleState(), settings.getMidiBindings());
        applyBindings();
        rebuildRows();
    };
    addAndMakeVisible (showOwnMappings);

    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    resetButton.onClick = [this]
    {
        stopLearning();
        storeBindings (makeDefaultMidiBindings());
    };
    resetButton.setTooltip ("Sustain pedal = next cue, CC 11 = expression 1, mod wheel = expression 2, "
                            "program change = recall");
    addAndMakeVisible (resetButton);

    rowsViewport.setViewedComponent (&rowsHolder, false);
    rowsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (rowsViewport);

    rebuildInputs();
    rebuildOutputs();
    rebuildRows();

    setSize (640, 600);
    startTimerHz (20);
}

MidiSettings::~MidiSettings()
{
    router.setLearning (false);
}

//==============================================================================
std::vector<MidiBinding> MidiSettings::currentBindings() const
{
    const auto& show = session.getShow();
    return show.ownMidiMappings ? show.midiBindings : settings.getMidiBindings();
}

void MidiSettings::storeBindings (const std::vector<MidiBinding>& bindings)
{
    if (session.getShow().ownMidiMappings)
        session.setShowMidiBindings (bindings);
    else
        settings.setMidiBindings (bindings);

    applyBindings();
    rebuildRows();
}

void MidiSettings::startLearning (int row)
{
    if (learningRow == row)
    {
        stopLearning();
        return;
    }

    learningRow = row;
    learnStartSequence = router.getLastReceived().sequence;
    learnStarted = juce::Time::getMillisecondCounter();
    router.setLearning (true);
    rebuildRows();
}

void MidiSettings::stopLearning()
{
    if (learningRow < 0)
        return;

    learningRow = -1;
    router.setLearning (false);
    rebuildRows();
}

void MidiSettings::showAddMenu()
{
    constexpr int idBase = 1000;
    juce::PopupMenu menu;
    addActionItems (menu, idBase);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safe = juce::Component::SafePointer<MidiSettings> (this)] (int result)
    {
        MidiBinding binding;

        if (safe == nullptr || ! decodeAction (result, idBase, binding.action, binding.index))
            return;

        if (binding.action == MidiAction::programRecall)
            binding.trigger.kind = MidiTrigger::Kind::programChange;

        auto bindings = safe->currentBindings();
        bindings.push_back (binding);
        safe->storeBindings (bindings);
        safe->startLearning ((int) bindings.size() - 1);   // the next thing you do is press it
    });
}

//==============================================================================
void MidiSettings::rebuildInputs()
{
    inputButtons.clear();
    shownInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& device : shownInputs)
    {
        auto* button = inputButtons.add (new juce::ToggleButton (device.name));
        button->setToggleState (deviceManager.isMidiInputDeviceEnabled (device.identifier), juce::dontSendNotification);
        button->onClick = [this, button, id = device.identifier]
        {
            // The device manager remembers this (saved with the audio settings)
            // and reopens the device when it's plugged back in.
            deviceManager.setMidiInputDeviceEnabled (id, button->getToggleState());
        };
        addAndMakeVisible (button);
    }

    resized();
}

void MidiSettings::rebuildOutputs()
{
    shownOutputs = juce::MidiOutput::getAvailableDevices();
    const auto wanted = settings.getMidiOutput();

    outputChoice.clear (juce::dontSendNotification);
    outputChoice.addItem ("None", 1);
    int selected = 1;

    for (int i = 0; i < shownOutputs.size(); ++i)
    {
        outputChoice.addItem (shownOutputs[i].name, i + 2);

        if (shownOutputs[i].identifier == wanted)
            selected = i + 2;
    }

    if (wanted.isNotEmpty() && selected == 1)
    {
        // Chosen but unplugged: keep the choice, say so.
        outputChoice.addItem ("(not connected) " + wanted, 999);
        selected = 999;
    }

    outputChoice.setSelectedId (selected, juce::dontSendNotification);
}

void MidiSettings::rebuildRows()
{
    const auto bindings = currentBindings();
    showOwnMappings.setToggleState (session.getShow().ownMidiMappings, juce::dontSendNotification);

    rows.clear();

    for (int i = 0; i < (int) bindings.size(); ++i)
    {
        auto* row = rows.add (new Row (*this, i));
        row->show (bindings[(size_t) i], i == learningRow);
        rowsHolder.addAndMakeVisible (row);
    }

    resized();
}

//==============================================================================
void MidiSettings::timerCallback()
{
    const auto last = router.getLastReceived();
    lastLabel.setText ("Last received: " + last.describe(), juce::dontSendNotification);

    if (learningRow >= 0)
    {
        auto bindings = currentBindings();

        if (! juce::isPositiveAndBelow (learningRow, (int) bindings.size())
            || juce::Time::getMillisecondCounter() - learnStarted > (juce::uint32) learnTimeoutMs)
        {
            stopLearning();
        }
        else if (last.sequence != learnStartSequence)
        {
            learnStartSequence = last.sequence;
            auto& binding = bindings[(size_t) learningRow];
            const auto type = last.status & 0xf0;

            // Pedals send a stream of CCs (any value counts); switches count
            // when pressed, so the release of a sustain pedal doesn't get learned.
            const auto usable = binding.action == MidiAction::expression     ? type == 0xb0
                              : binding.action == MidiAction::programRecall  ? type == 0xc0
                                                                             : last.isPress();
            if (usable)
            {
                binding.trigger = last.toTrigger();
                learningRow = -1;
                router.setLearning (false);
                storeBindings (bindings);
            }
        }
    }

    // Devices come and go (USB, Bluetooth): refresh the lists once a second.
    if (++tick % 20 == 0)
    {
        if (juce::MidiInput::getAvailableDevices() != shownInputs)
            rebuildInputs();

        if (juce::MidiOutput::getAvailableDevices() != shownOutputs)
            rebuildOutputs();
    }
}

//==============================================================================
void MidiSettings::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);
}

void MidiSettings::resized()
{
    auto area = getLocalBounds().reduced (16);

    inputsLabel.setBounds (area.removeFromTop (26));

    if (inputButtons.isEmpty())
        area.removeFromTop (4);

    for (auto* button : inputButtons)
        button->setBounds (area.removeFromTop (26).withTrimmedLeft (8));

    lastLabel.setBounds (area.removeFromTop (24).withTrimmedLeft (8));
    area.removeFromTop (10);

    outputLabel.setBounds (area.removeFromTop (26));
    outputChoice.setBounds (area.removeFromTop (28).withTrimmedLeft (8).withWidth (360));
    area.removeFromTop (14);

    auto header = area.removeFromTop (28);
    mappingsLabel.setBounds (header.removeFromLeft (120));
    resetButton.setBounds (header.removeFromRight (140).reduced (0, 1));
    header.removeFromRight (8);
    addButton.setBounds (header.removeFromRight (130).reduced (0, 1));

    showOwnMappings.setBounds (area.removeFromTop (28).withTrimmedLeft (8));
    area.removeFromTop (6);

    rowsViewport.setBounds (area);

    constexpr int rowHeight = 32;
    const auto width = area.getWidth() - rowsViewport.getScrollBarThickness() - 4;
    rowsHolder.setSize (juce::jmax (0, width), rows.size() * rowHeight);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, rowsHolder.getWidth(), rowHeight);
}

} // namespace encore
