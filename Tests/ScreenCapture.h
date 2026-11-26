// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// What the screen really shows, for tests of real windows (Windows only):
// layered windows (menus, shadows) included. Free of JUCE so its .cpp can
// include <windows.h> on its own.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace encore::test::screen
{

/** 0xAARRGGBB pixels, row by row, of a rectangle in physical screen pixels.
    Empty (and why) where it can't capture. */
std::vector<std::uint32_t> capture (int x, int y, int width, int height, std::string& error);

/** Native facts about a window: visible, its rectangle, its extended style. */
std::string describeWindow (void* nativeWindow);

/** How many top-level windows are above this native window (-1 where unknown). */
int windowsAbove (void* nativeWindow);

} // namespace encore::test::screen
