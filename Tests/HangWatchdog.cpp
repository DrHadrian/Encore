// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "HangWatchdog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <tlhelp32.h>
 #include <dbghelp.h>   // linked in CMakeLists.txt
#endif

namespace encore::testing
{
namespace
{
    using Clock = std::chrono::steady_clock;

    long long ticksNow() noexcept
    {
        return static_cast<long long> (Clock::now().time_since_epoch().count());
    }

    std::atomic<long long> lastProgress { ticksNow() };

    std::mutex stepLock;
    std::string currentStep = "(before the first test)";

   #if defined (_WIN32)
    DWORD testThreadId = 0;

    /** Every other thread's stack, symbolised (needs the PDB: CMakeLists.txt
        links EncoreTests with /DEBUG). Each thread is suspended only while its
        return addresses are copied; nothing is allocated or printed meanwhile,
        so a thread holding the heap or the console can't block this. */
    void printStacks()
    {
       #if defined (_M_X64)
        const auto process = GetCurrentProcess();
        const auto self = GetCurrentThreadId();
        const auto pid = GetCurrentProcessId();

        // Symbols next to the program (and where the linker wrote them).
        char folder[MAX_PATH] = {};
        const auto length = GetModuleFileNameA (nullptr, folder, MAX_PATH);

        for (auto i = static_cast<int> (length); i > 0; --i)
        {
            if (folder[i - 1] == '\\' || folder[i - 1] == '/')
            {
                folder[i - 1] = 0;
                break;
            }
        }

        SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        const auto symbols = SymInitialize (process, folder, TRUE) != FALSE;

        if (! symbols)
            std::printf ("(no symbols: error %lu)\n", static_cast<unsigned long> (GetLastError()));

        const auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);

        if (snapshot == INVALID_HANDLE_VALUE)
        {
            std::printf ("(can't list the threads: error %lu)\n", static_cast<unsigned long> (GetLastError()));
            return;
        }

        THREADENTRY32 entry {};
        entry.dwSize = sizeof (entry);

        for (auto more = Thread32First (snapshot, &entry); more != FALSE; more = Thread32Next (snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self)
                continue;

            const auto thread = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                            FALSE, entry.th32ThreadID);

            if (thread == nullptr)
                continue;

            constexpr int maxFrames = 64;
            DWORD64 frames[maxFrames] = {};
            int count = 0;

            if (SuspendThread (thread) != static_cast<DWORD> (-1))
            {
                CONTEXT context {};
                context.ContextFlags = CONTEXT_FULL;

                if (GetThreadContext (thread, &context) != FALSE)
                {
                    while (count < maxFrames && context.Rip != 0 && context.Rsp != 0)
                    {
                        frames[count++] = context.Rip;
                        DWORD64 imageBase = 0;

                        if (auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr))
                        {
                            void* handlerData = nullptr;
                            DWORD64 establisherFrame = 0;
                            RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
                                              &context, &handlerData, &establisherFrame, nullptr);
                        }
                        else
                        {
                            // A leaf function: the return address is on top of the stack.
                            context.Rip = *reinterpret_cast<const DWORD64*> (context.Rsp);
                            context.Rsp += 8;
                        }
                    }
                }

                ResumeThread (thread);
            }

            CloseHandle (thread);

            std::printf ("\nThread %lu%s:\n", static_cast<unsigned long> (entry.th32ThreadID),
                         entry.th32ThreadID == testThreadId ? " (the test thread)" : "");

            for (int i = 0; i < count; ++i)
            {
                // A return address points after the call: look up the call itself.
                const auto address = i == 0 ? frames[i] : frames[i] - 1;
                alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512] = {};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
                symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
                symbol->MaxNameLen = 511;
                DWORD64 displacement = 0;
                IMAGEHLP_LINE64 line {};
                line.SizeOfStruct = sizeof (line);
                DWORD lineDisplacement = 0;

                if (symbols && SymFromAddr (process, address, &displacement, symbol) != FALSE)
                {
                    if (SymGetLineFromAddr64 (process, address, &lineDisplacement, &line) != FALSE)
                        std::printf ("  %2d  %s  (%s:%lu)\n", i, symbol->Name, line.FileName,
                                     static_cast<unsigned long> (line.LineNumber));
                    else
                        std::printf ("  %2d  %s + 0x%llx\n", i, symbol->Name,
                                     static_cast<unsigned long long> (displacement));
                }
                else
                {
                    std::printf ("  %2d  0x%llx\n", i, static_cast<unsigned long long> (address));
                }
            }
        }

        CloseHandle (snapshot);

        if (symbols)
            SymCleanup (process);
       #else
        std::printf ("(stacks are only printed on x64)\n");
       #endif
    }
   #endif

    [[noreturn]] void reportAndExit (long long idleSeconds)
    {
        std::string step = "(unknown: the step is being written)";

        if (std::unique_lock<std::mutex> lock { stepLock, std::try_to_lock }; lock.owns_lock())
            step = currentStep;

        std::printf ("\nHANG: no progress for %lld s. The last step: %s\n", idleSeconds, step.c_str());
        std::fflush (stdout);

       #if defined (_WIN32)
        printStacks();
        std::printf ("\nHANG: the test run was ended (ENCORE_TEST_HANG_SECONDS).\n");
        std::fflush (stdout);
        TerminateProcess (GetCurrentProcess(), 3);   // no clean-up: other threads may be stuck in it
       #endif

        std::_Exit (3);
    }
}

struct HangWatchdog::Impl
{
    explicit Impl (int s) : seconds (s), thread ([this] { run(); }) {}

    ~Impl()
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            stopping = true;
        }

        wake.notify_all();
        thread.join();
    }

    void run()
    {
        std::unique_lock<std::mutex> l (lock);

        while (! stopping)
        {
            wake.wait_for (l, std::chrono::seconds (1));

            if (stopping)
                return;

            const auto idle = std::chrono::duration_cast<std::chrono::seconds> (
                                  Clock::duration (static_cast<Clock::rep> (ticksNow() - lastProgress.load())));

            if (idle.count() >= seconds)
                reportAndExit (static_cast<long long> (idle.count()));
        }
    }

    const int seconds;
    std::mutex lock;
    std::condition_variable wake;
    bool stopping = false;
    std::thread thread;   // last: starts once the rest is set up
};

HangWatchdog::HangWatchdog()
{
    const auto* text = std::getenv ("ENCORE_TEST_HANG_SECONDS");
    const auto seconds = text != nullptr ? std::atoi (text) : 0;

    if (seconds <= 0)
        return;

   #if defined (_WIN32)
    testThreadId = GetCurrentThreadId();
   #endif

    heartbeat();
    impl = new Impl (seconds);
}

HangWatchdog::~HangWatchdog()
{
    delete impl;
}

void HangWatchdog::step (const std::string& what)
{
    {
        const std::lock_guard<std::mutex> l (stepLock);
        currentStep = what;
    }

    heartbeat();
}

void heartbeat() noexcept
{
    lastProgress.store (ticksNow());
}

} // namespace encore::testing
