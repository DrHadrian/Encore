// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "platform/SystemTuning.h"

#include <atomic>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <avrt.h>
 #include <psapi.h>
 #pragma comment (lib, "avrt.lib")
 #pragma comment (lib, "psapi.lib")

 // Older Windows SDKs lack the newest flag; the value is stable.
 #ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
  #define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
 #endif
#endif

namespace encore::system
{

namespace
{
    std::atomic<bool> anyThreadRegistered { false };
}

TuningReport applyLiveProcessSettings()
{
    TuningReport report;

   #if JUCE_WINDOWS
    // 1. Never throttle us. ControlMask names the policies we decide about;
    //    a StateMask of 0 means "off" for all of them.
    PROCESS_POWER_THROTTLING_STATE throttling {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED
                           | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.StateMask = 0;
    report.powerThrottlingDisabled = SetProcessInformation (GetCurrentProcess(), ProcessPowerThrottling,
                                                            &throttling, sizeof (throttling)) != 0;

    // Windows versions before 11 don't know the timer-resolution flag; retry without it.
    if (! report.powerThrottlingDisabled)
    {
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        report.powerThrottlingDisabled = SetProcessInformation (GetCurrentProcess(), ProcessPowerThrottling,
                                                                &throttling, sizeof (throttling)) != 0;
    }

    // 2. High (not Realtime, which can starve the mouse and the driver itself).
    report.highPriority = SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS) != 0;

    // 3. No sleep, no screen-off while Encore runs.
    report.keepAwake = SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) != 0;
   #endif

    return report;
}

void ensureAudioThreadIsProAudio() noexcept
{
   #if JUCE_WINDOWS
    thread_local bool registered = false;

    if (registered)
        return;

    registered = true;   // try once per thread, even if it fails

    // A one-time system call on the audio thread. It doesn't touch the C++
    // heap; the registration lasts as long as the driver's thread does.
    DWORD taskIndex = 0;

    if (auto handle = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex))
    {
        AvSetMmThreadPriority (handle, AVRT_PRIORITY_CRITICAL);
        anyThreadRegistered.store (true);
    }
   #endif
}

bool isAudioThreadProAudio() noexcept
{
    return anyThreadRegistered.load();
}

juce::int64 getProcessMemoryBytes()
{
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};

    if (GetProcessMemoryInfo (GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&counters), sizeof (counters)) != 0)
        return (juce::int64) counters.PrivateUsage;
   #elif JUCE_LINUX
    // Resident pages, from /proc (good enough for development builds).
    const auto statm = juce::File ("/proc/self/statm").loadFileAsString();
    const auto pages = juce::StringArray::fromTokens (statm, " ", {});

    if (pages.size() > 1)
        return (juce::int64) pages[1].getLargeIntValue() * 4096;
   #endif

    return 0;
}

PowerSource getPowerSource()
{
   #if JUCE_WINDOWS
    SYSTEM_POWER_STATUS status {};

    if (GetSystemPowerStatus (&status) != 0)
    {
        if (status.ACLineStatus == 1) return PowerSource::mains;
        if (status.ACLineStatus == 0) return PowerSource::battery;
    }
   #endif

    return PowerSource::unknown;
}

} // namespace encore::system
