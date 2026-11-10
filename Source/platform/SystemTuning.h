// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

namespace encore::system
{

/** What applyLiveProcessSettings() managed to do, for the status line. */
struct TuningReport
{
    bool powerThrottlingDisabled = false;
    bool highPriority = false;
    bool keepAwake = false;
};

/** Process-wide settings DAWs use so Windows never slows the audio down:
      - opt out of power throttling ("efficiency mode"), which otherwise slows
        an app's threads and timers whenever it isn't the foreground window,
        especially on battery;
      - High process priority, so background work can't push Encore aside;
      - keep the system and display awake (no sleep or screen-off mid-show).
    Call once at start-up, on the message thread. No-op on other platforms.
*/
TuningReport applyLiveProcessSettings();

/** Call at the top of every audio callback. The first time it runs on a given
    thread, it registers that thread with Windows' "Pro Audio" scheduling class
    (MMCSS) at critical priority, so the scheduler treats it as time-critical.
    Afterwards it costs one thread-local check. Safe if the driver already did it.
*/
void ensureAudioThreadIsProAudio() noexcept;

/** True once some audio thread has been registered as Pro Audio. */
bool isAudioThreadProAudio() noexcept;

/** Memory Encore is using (private bytes), or 0 if unknown. */
juce::int64 getProcessMemoryBytes();

/** Power source, for the "plug in for shows" warning. */
enum class PowerSource { mains, battery, unknown };
PowerSource getPowerSource();

} // namespace encore::system
