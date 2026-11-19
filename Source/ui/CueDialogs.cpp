// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/CueDialogs.h"

namespace encore::cuedialogs
{

void editProgramChange (juce::Component& parent, ShowSession& session, int row)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (row, (int) cues.size()))
        return;

    const auto current = cues[(size_t) row]->programChange;
    auto* window = new juce::AlertWindow ("Recall with a program change",
                                          "The program number (0-127) your controller sends to jump straight to this cue. "
                                          "Leave empty for none.",
                                          juce::MessageBoxIconType::NoIcon, &parent);
    window->addTextEditor ("program", current >= 0 ? juce::String (current) : juce::String());
    window->getTextEditor ("program")->setInputRestrictions (3, "0123456789");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::Component> safeThis (&parent);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, &session, window, row] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        const auto text = window->getTextEditorContents ("program").trim();
        session.setCueProgramChange (row, text.isEmpty() ? -1 : juce::jlimit (0, 127, text.getIntValue()));
    }), true);
}

void editMidiOut (juce::Component& parent, ShowSession& session, int row)
{
    const auto cues = session.getCues();

    if (! juce::isPositiveAndBelow (row, (int) cues.size()))
        return;

    auto* window = new juce::AlertWindow ("MIDI out when this cue starts",
                                          "One message per line, sent to the MIDI output chosen in MIDI settings:\n"
                                          "  PC <channel> <program>        e.g.  PC 1 5\n"
                                          "  CC <channel> <number> <value>  e.g.  CC 1 20 127",
                                          juce::MessageBoxIconType::NoIcon, &parent);
    window->addTextEditor ("midi", formatMidiOut (cues[(size_t) row]->midiOut));

    auto* editor = window->getTextEditor ("midi");
    editor->setMultiLine (true);
    editor->setReturnKeyStartsNewLine (true);
    editor->setSize (editor->getWidth(), 110);
    window->addButton ("OK", 1);
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::Component> safeThis (&parent);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, &session, window, row] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        std::vector<MidiOutMessage> messages;
        juce::String error;

        if (parseMidiOut (window->getTextEditorContents ("midi"), messages, error))
        {
            session.setCueMidiOut (row, std::move (messages));
            return;
        }

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "MIDI out not changed", error, {}, safeThis.getComponent());
    }), true);
}

} // namespace encore::cuedialogs
