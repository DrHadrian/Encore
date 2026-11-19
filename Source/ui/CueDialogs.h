// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/ShowSession.h"

namespace encore::cuedialogs
{

/** Small dialogs for a cue's MIDI, shared by the set list and the cue list.
    `cueIndex` is the playable cue's position. The session must outlive them
    (it does: the main window owns it and closes dialogs first). */
void editProgramChange (juce::Component& parent, ShowSession& session, int cueIndex);
void editMidiOut (juce::Component& parent, ShowSession& session, int cueIndex);

} // namespace encore::cuedialogs
