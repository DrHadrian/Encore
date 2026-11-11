// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/MidiSettings.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    void noFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
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
        menu.addSeparator();
        menu.addItem (id (MidiAction::mute, 1), describe (MidiAction::mute, 1));
        menu.addItem (id (MidiAction::tuner, 1), describe (MidiAction::tuner, 1));   // the stale "(from milestone 7)" is gone
        menu.addItem (id (MidiAction::panic, 1), describe (MidiAction::panic, 1));
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
/** One mapping: [what it does ▾] [which control] [Learn] [✕] */
class MidiSettings::Row final : public juce::Component
{
public:
    Row (MidiSettings& o, int i) : owner (o), rowIndex (i)
    {
        setVariant (actionButton, Variant::popup);
        actionButton.onClick = [this] { chooseAction(); };
        actionButton.setTooltip ("What this control does (click to change)");

        learnButton.setTooltip ("Click, then press the footswitch (or move the pedal)");
        // Row buttons act after their click returns: the action rebuilds the
        // rows, and a button mustn't be deleted from inside its own onClick.
        learnButton.onClick = [this] { later ([] (MidiSettings& settings, int row) { settings.startLearning (row); }); };

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

        for (auto* c : std::initializer_list<juce::Component*> { &actionButton, &learnButton, &removeButton })
        {
            noFocus (*c);
            addAndMakeVisible (c);
        }
    }

    void show (const MidiBinding& binding, bool isLearning)
    {
        learning = isLearning;
        actionButton.setButtonText (describe (binding.action, binding.index));
        trigger = learning ? juce::String (juce::CharPointer_UTF8 ("Press the control now\xe2\x80\xa6")) : describe (binding.trigger);
        learnButton.setButtonText (learning ? "Cancel" : "Learn");
        learnButton.setToggleState (learning, juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (learning)
            g.fillAll (paint::c (colour::learningRow));
        else if (rowIndex % 2 == 1)
            g.fillAll (paint::c (colour::mappingRowAlt));

        paint::text (g, trigger, triggerBox, type::body, learning ? colour::amber : colour::triggerText);
    }

    void resized() override
    {
        const auto r = getLocalBounds();
        const auto buttonH = size::buttonHeight + size::buttonDrop;
        actionButton.setBounds (size::mappingPadX, (r.getHeight() - size::buttonHeight) / 2, size::actionW, buttonH);
        removeButton.setBounds (r.getRight() - size::removeRight - size::removeW, (r.getHeight() - size::removeW) / 2, size::removeW, size::removeW);
        learnButton.setBounds (removeButton.getX() - size::deviceRowGap - size::learnW, (r.getHeight() - size::learnH) / 2, size::learnW, size::learnH + size::buttonDrop);
        triggerBox = juce::Rectangle<int> (size::triggerX, 0, juce::jmax (0, learnButton.getX() - size::deviceRowGap - size::triggerX), r.getHeight()).toFloat();
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
    juce::TextButton actionButton, learnButton { "Learn" };
    kit::IconButton removeButton { Icon::close, "Remove mapping" };
    juce::String trigger;
    juce::Rectangle<float> triggerBox;
    bool learning = false;
};

//==============================================================================
MidiSettings::MidiSettings (juce::AudioDeviceManager& dm, MidiRouter& r, AppSettings& s, ShowSession& ss,
                            std::function<void()> bindingsChanged, std::function<void()> outputChanged)
    : deviceManager (dm), router (r), settings (s), session (ss),
      applyBindings (std::move (bindingsChanged)), applyOutput (std::move (outputChanged))
{
    for (auto* group : { &listenGroup, &outputGroup, &mappingsGroup, &howGroup, &defaultsGroup })
        addAndMakeVisible (group);

    // The table's column header and bottom bar, behind their buttons.
    for (auto* fill : { &headerFill, &barFill })
    {
        fill->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (fill);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &outputChoice, &showOwnMappings, &addButton, &resetButton })
        noFocus (*c);

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

    // The help on the right, with its bold words (laid out once).
    {
        juce::AttributedString help;
        const auto add = [&help] (const juce::String& text, bool strong)
        {
            help.append (text, font (strong ? type::helpStrong : type::helpText), paint::c (strong ? colour::text : colour::textSecondary));
        };
        const auto dash = juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 "));
        add ("Learn", true);
        add (dash + "click, then press the footswitch or move the pedal.\n\n", false);
        add ("Click the action", true);
        add (" to change what a control does, ", false);
        add (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")), true);
        add (" to remove it.\n\nFootswitches act once per press, also while Encore is in the background.\n\nWhat ", false);
        add ("Stomp 3", true);
        add (" switches and which knob ", false);
        add ("Expression 1", true);
        add (" moves is set per patch: right-click a block or a knob in Edit.", false);
        help.setLineSpacing ((float) size::helpLineH - font (type::helpText).getHeight());
        howText.createLayout (help, (float) (size::midiRightW - 2 * size::groupPad));
    }

    rebuildInputs();
    rebuildOutputs();
    rebuildRows();

    setSize (size::settingsW, getIdealHeight());
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
        noFocus (*button);
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
    if (const auto text = last.describe(); text != lastReceived)
    {
        lastReceived = text;
        lastReceivedW = textWidth (font (type::bodySmall), lastReceived);
        repaint (mappingsBox.withTop (mappingsBox.getBottom() - size::mappingBarH));
    }

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
void MidiSettings::Fill::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::mappingHeader));

    if (lineOnTop)
    {
        g.setColour (paint::c (colour::paneHeaderLine));
        paint::hline (g, 0.0f, 0.0f, (float) getWidth());
    }
}

int MidiSettings::getIdealHeight() const
{
    // The mappings fill the dialog; a few rows at least when it's small.
    return size::settingsH - size::dialogTitleBar - size::tabStrip - size::dialogFooter - 2;
}

void MidiSettings::resized()
{
    const auto pad = size::dialogPadX;
    const auto columnGap = size::settingsColumnGap - 2;
    auto y = size::midiPadTop;

    // LISTEN TO | SEND CUE MESSAGES TO
    const auto listenBoxH = 2 * size::midiInputTop + juce::jmax (1, inputButtons.size()) * size::midiInputRowH - size::midiInputTop / 2;
    listenGroup.setBounds (pad, y, size::midiColumnW, kit::Group::heightFor (listenBoxH));
    {
        const auto box = listenGroup.getBoxBounds() + listenGroup.getPosition();

        for (int i = 0; i < inputButtons.size(); ++i)
            inputButtons[i]->setBounds (box.getX() + size::groupPad, box.getY() + size::midiInputTop / 2 + i * size::midiInputRowH,
                                        box.getWidth() - 2 * size::groupPad, size::midiInputRowH);
    }

    const auto outputX = pad + size::midiColumnW + columnGap;
    outputGroup.setBounds (outputX, y, size::midiColumnW, kit::Group::heightFor (size::mappingPadX + size::buttonHeight + 3 * size::helpLineH + size::mappingPadX));
    {
        const auto box = outputGroup.getBoxBounds() + outputGroup.getPosition();
        outputChoice.setBounds (box.getX() + size::mappingPadX, box.getY() + size::mappingPadX - 1, box.getWidth() - 2 * size::mappingPadX,
                                size::buttonHeight + size::buttonDrop);
    }

    // MAPPINGS: the toggle on its title line; the table to the bottom.
    y = juce::jmax (listenGroup.getBottom(), outputGroup.getBottom()) + size::groupGap;
    const auto mappingsW = outputGroup.getRight() - pad;
    mappingsGroup.setBounds (pad, y, mappingsW, juce::jmax (kit::Group::heightFor (size::mappingHeaderH + 3 * size::mappingRowH + size::mappingBarH),
                                                            getHeight() - y - size::groupGap));
    const auto toggleW = juce::roundToInt (textWidth (font (type::body), showOwnMappings.getButtonText())) + size::checkRowH;
    showOwnMappings.setBounds (mappingsGroup.getRight() - toggleW, y - size::checkHeadGap / 2, toggleW, size::groupTitleH);
    mappingsBox = mappingsGroup.getBoxBounds() + mappingsGroup.getPosition();

    auto table = mappingsBox.reduced (1);
    table.removeFromTop (size::mappingHeaderH);
    auto bar = table.removeFromBottom (size::mappingBarH);
    headerFill.setBounds (mappingsBox.reduced (1).withHeight (size::mappingHeaderH));
    barFill.setBounds (bar);
    rowsViewport.setBounds (table);
    rowsHolder.setSize (table.getWidth() - (rows.size() * size::mappingRowH > table.getHeight() ? rowsViewport.getScrollBarThickness() : 0),
                        rows.size() * size::mappingRowH);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * size::mappingRowH, rowsHolder.getWidth(), size::mappingRowH);

    const auto buttonH = size::buttonHeight + size::buttonDrop;
    const auto barY = bar.getY() + (bar.getHeight() - size::buttonHeight) / 2;
    addButton.setBounds (bar.getX() + size::mappingPadX - 1, barY, addButton.getBestWidthForHeight (buttonH), buttonH);
    resetButton.setBounds (addButton.getRight() + size::layoutToolbarGap, barY, resetButton.getBestWidthForHeight (buttonH), buttonH);

    // HOW MAPPINGS WORK, DEFAULTS
    const auto rightX = outputGroup.getRight() + size::settingsColumnGap;
    howGroup.setBounds (rightX, size::midiPadTop, size::midiRightW,
                        kit::Group::heightFor (juce::roundToInt (howText.getHeight()) + 2 * size::groupPad - 2));
    defaultsGroup.setBounds (rightX, howGroup.getBottom() + size::groupGap - 2, size::midiRightW,
                             kit::Group::heightFor (2 * size::groupPad - 2 + 4 * size::statusRowH));
}

void MidiSettings::paintOverChildren (juce::Graphics& g)
{
    // The words, made once (nothing is built while painting).
    static const juce::String noMappings ("No mappings: + Add mapping, or Reset to defaults");
    static const juce::String defaultsText[][2] = { { "Sustain pedal (CC 64)", "Next cue" }, { "CC 11", "Expression 1" },
                                                    { "Mod wheel (CC 1)", "Expression 2" }, { "Program change", "Program recall" } };

    if (inputButtons.isEmpty())
        paint::text (g, noInputs, (listenGroup.getBoxBounds() + listenGroup.getPosition()).toFloat().reduced ((float) size::groupPad, 0.0f),
                     type::body, colour::textTertiary);

    // The output's help line.
    {
        const auto box = outputGroup.getBoxBounds() + outputGroup.getPosition();
        const auto text = juce::Rectangle<int> (box.getX() + size::mappingPadX, outputChoice.getBottom() + size::layoutToolbarGap,
                                                box.getWidth() - 2 * size::mappingPadX, 3 * size::helpLineH - 4);
        g.setColour (paint::c (colour::textTertiary));
        g.setFont (font (type::noteText));
        g.drawFittedText (outputHelp, text, juce::Justification::topLeft, 3, 1.0f);
    }

    // The table's column heads and the bottom bar with the last message received.
    {
        const auto inner = mappingsBox.reduced (1).toFloat();
        const auto head = inner.withHeight ((float) size::mappingHeaderH);
        paint::text (g, doesHead, head.withTrimmedLeft ((float) size::mappingPadX), type::columnHead, colour::textDisabled);
        paint::text (g, controlHead, head.withTrimmedLeft ((float) size::triggerX), type::columnHead, colour::textDisabled);
        const auto bar = inner.withTop (inner.getBottom() - (float) size::mappingBarH);

        if (rows.isEmpty())
            paint::text (g, noMappings, inner.withTop (head.getBottom()).withBottom (bar.getY()),
                         type::body, colour::textTertiary, juce::Justification::centred);

        // "Last received" and the value in a small LCD.
        const auto valueW = juce::jmax ((float) size::lastValueW, lastReceivedW + 2.0f * (float) size::fieldPadX);
        const auto value = juce::Rectangle<float> (bar.getRight() - (float) size::mappingPadX - valueW,
                                                   bar.getCentreY() - (float) size::lastValueH * 0.5f, valueW, (float) size::lastValueH);
        g.setColour (paint::c (colour::lcdBot));
        g.fillRoundedRectangle (value, (float) size::fieldRadius);
        g.setColour (paint::c (colour::lcdBorder));
        g.drawRoundedRectangle (value.reduced (0.5f), (float) size::fieldRadius, 1.0f);
        paint::text (g, lastReceived, value.reduced ((float) size::fieldPadX, 0.0f), type::bodySmall, colour::lcdText, juce::Justification::centred);
        paint::text (g, lastLabel, value.withX (value.getX() - (float) size::lastValueW).withWidth ((float) size::lastValueW - (float) size::layoutToolbarGap),
                     type::noteText, colour::textTertiary, juce::Justification::centredRight);
    }

    // The help groups.
    {
        const auto box = (howGroup.getBoxBounds() + howGroup.getPosition()).toFloat().reduced ((float) size::groupPad, (float) size::groupPad - 2.0f);
        howText.draw (g, box);

        const auto defaults = (defaultsGroup.getBoxBounds() + defaultsGroup.getPosition()).toFloat().reduced ((float) size::groupPad, (float) size::groupPad - 2.0f);

        for (int i = 0; i < 4; ++i)
        {
            const auto row = defaults.withY (defaults.getY() + (float) (i * size::statusRowH)).withHeight ((float) size::statusTextH);
            paint::text (g, defaultsText[i][0], row, type::helpText, colour::textSecondary);
            paint::text (g, defaultsText[i][1], row.withTrimmedLeft ((float) size::defaultsValueX), type::helpText, colour::text);
        }
    }
}

} // namespace encore
