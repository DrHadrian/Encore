// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

namespace encore::eventlog
{

/** A log file per run in %APPDATA%\Encore\Logs (the last 30 are kept):
    start-up, devices, reconnects, recoveries, problems. Writes go through
    juce::Logger, so this is for the message thread, NEVER the audio thread.

    Also installs a crash handler that writes the stack to crash-<time>.txt
    in the same folder before Encore dies, and, if the crash happened inside
    a plugin, which one to plugin-crash.txt next to the Logs folder (that
    block starts switched off next time, decision 0046). */
void start (const juce::File& logsFolder, const juce::String& appVersion);
void stop();

void write (const juce::String& message);

juce::File getLogsFolder();

/** "<block id>\t<plugin name>" after a crash inside a plugin (beside the Logs folder). */
juce::File getPluginCrashFile();

} // namespace encore::eventlog
