// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

namespace encore::system
{

/** What the crash handler can say about a crash, from what Windows hands it
    (EXCEPTION_POINTERS) and the crashing thread's stack. Elsewhere it knows
    less (only the stack). For crash-<time>.txt and for finding the plugin
    that crashed (decision 0046). */
struct CrashFacts
{
    juce::String what;             // "Access violation writing address 0x10 (0xC0000005)"
    juce::String module;           // the file the crashing code is in (a DLL, a .vst3)
    juce::String moduleOffset;     // "0x679331" into that file
    juce::StringArray stack;       // "<file> + 0x<offset>", innermost first (no frame skipped)
    juce::StringArray stackModules;// the file of each frame, same order
};

/** Call from the crash handler only (with what it was given). */
CrashFacts describeCrash (void* exceptionPointers);

/** The file (EXE, DLL) the code at this address belongs to, empty if none. */
juce::String moduleForAddress (const void* address);

} // namespace encore::system
