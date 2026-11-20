// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// Real touches for tests: Windows' touch injection (no touch screen needed).
// Kept free of JUCE so its .cpp can include <windows.h> on its own.

#pragma once

#include <string>
#include <vector>

namespace encore::test::touch
{

struct Contact
{
    enum class Phase { down, move, up };

    int id = 0;
    int x = 0, y = 0;   // physical screen pixels
    Phase phase = Phase::down;
};

/** Once per process, before inject(). False (and why) where it can't work. */
bool initialise (int maxContacts, std::string& error);

/** One frame: every finger that is touching must be in it. */
bool inject (const std::vector<Contact>& frame, std::string& error);

} // namespace encore::test::touch
