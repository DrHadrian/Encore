// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "model/ShowModel.h"

namespace encore::json
{

/** Show <-> JSON text. Human-readable (diffs well in git), with a schema
    version so older files can be migrated and newer ones refused clearly.
    Missing fields fall back to defaults, so hand-edited files stay loadable. */
juce::String toJson (const ShowModel& show);

/** Returns the show, or nothing and an error message. */
std::optional<ShowModel> fromJson (const juce::String& text, juce::String& error);

// Exposed for tests and for copying single patches between shows later.
juce::var patchToVar (const PatchModel& patch);
PatchModel patchFromVar (const juce::var& v);

} // namespace encore::json
