// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Precompiled once per target and put in front of every Encore source file
// (CMake: ENCORE_PCH, decision 0049). Parsing these headers is about 95 % of
// the time it takes to compile a typical Encore file, so they go here; only
// stable headers belong in this list, never Encore's own.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
