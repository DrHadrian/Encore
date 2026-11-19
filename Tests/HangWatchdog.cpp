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
 #include <winternl.h>
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

   #if defined (_M_X64)
    // The report must work when the Windows loader lock is held (a stuck thread
    // start or exit, or a DLL load): the first version stopped at its first
    // loader call. So the stacks are taken without the loader, the heap or the
    // console, printed as module + offset, and only then named by a helper
    // thread that may itself never start; it gets 30 s.
    constexpr int maxThreads = 64;
    constexpr int maxFrames = 48;
    constexpr int maxModules = 160;

    struct Frame { DWORD64 pc = 0, base = 0; };
    struct ThreadStack { DWORD id = 0; int count = 0; Frame frames[maxFrames]; };
    struct Module { DWORD64 base = 0; char name[64] = {}; };

    ThreadStack stacks[maxThreads];
    int threadCount = 0;
    Module modules[maxModules];
    int moduleCount = 0;

    /** The process's module list, read directly (no loader lock). */
    void listModules()
    {
        auto* peb = NtCurrentTeb()->ProcessEnvironmentBlock;

        if (peb == nullptr || peb->Ldr == nullptr)
            return;

        LIST_ENTRY* head = &peb->Ldr->InMemoryOrderModuleList;

        for (auto* link = head->Flink; link != nullptr && link != head && moduleCount < maxModules; link = link->Flink)
        {
            auto* entry = CONTAINING_RECORD (link, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
            auto& module = modules[moduleCount++];
            module.base = reinterpret_cast<DWORD64> (entry->DllBase);

            const wchar_t* text = entry->FullDllName.Buffer;
            const auto chars = text != nullptr ? static_cast<size_t> (entry->FullDllName.Length / sizeof (wchar_t)) : 0;
            size_t start = 0;

            for (size_t i = 0; i < chars; ++i)
                if (text[i] == L'\\' || text[i] == L'/')
                    start = i + 1;

            size_t n = 0;

            for (size_t i = start; i < chars && n + 1 < sizeof (module.name); ++i)
                module.name[n++] = text[i] < 128 ? static_cast<char> (text[i]) : '?';

            module.name[n] = 0;
        }
    }

    const char* moduleName (DWORD64 base)
    {
        for (int i = 0; i < moduleCount; ++i)
            if (modules[i].base == base)
                return modules[i].name;

        return "?";
    }

    /** Walks one suspended thread's stack. No C++ objects here, so a bad
        stack can be caught (SEH) instead of ending the report. */
    int unwind (CONTEXT& context, Frame* frames)
    {
        int count = 0;

        __try
        {
            while (count < maxFrames && context.Rip != 0 && context.Rsp != 0)
            {
                auto& frame = frames[count++];
                frame.pc = context.Rip;
                DWORD64 imageBase = 0;

                if (auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr))
                {
                    frame.base = imageBase;
                    void* handlerData = nullptr;
                    DWORD64 establisherFrame = 0;
                    RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
                                      &context, &handlerData, &establisherFrame, nullptr);
                }
                else
                {
                    PVOID base = nullptr;
                    RtlPcToFileHeader (reinterpret_cast<PVOID> (context.Rip), &base);
                    frame.base = reinterpret_cast<DWORD64> (base);

                    if (count > 1)
                        break;   // only the top frame can be a leaf function

                    context.Rip = *reinterpret_cast<const DWORD64*> (context.Rsp);
                    context.Rsp += 8;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    void captureStacks()
    {
        const auto self = GetCurrentThreadId();
        const auto pid = GetCurrentProcessId();
        const auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);

        if (snapshot == INVALID_HANDLE_VALUE)
            return;

        THREADENTRY32 entry {};
        entry.dwSize = sizeof (entry);

        for (auto more = Thread32First (snapshot, &entry); more != FALSE && threadCount < maxThreads;
             more = Thread32Next (snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self)
                continue;

            const auto thread = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                            FALSE, entry.th32ThreadID);

            if (thread == nullptr)
                continue;

            auto& stack = stacks[threadCount++];
            stack.id = entry.th32ThreadID;
            stack.count = 0;

            if (SuspendThread (thread) != static_cast<DWORD> (-1))
            {
                CONTEXT context {};
                context.ContextFlags = CONTEXT_FULL;

                if (GetThreadContext (thread, &context) != FALSE)
                    stack.count = unwind (context, stack.frames);

                ResumeThread (thread);
            }

            CloseHandle (thread);
        }

        CloseHandle (snapshot);
    }

    const char* threadLabel (DWORD id)
    {
        return id == testThreadId ? " (the test thread)" : "";
    }

    void printRaw()
    {
        std::printf ("\nStacks (module + offset):\n");

        for (int t = 0; t < threadCount; ++t)
        {
            const auto& stack = stacks[t];
            std::printf ("\nThread %lu%s:\n", static_cast<unsigned long> (stack.id), threadLabel (stack.id));

            for (int i = 0; i < stack.count; ++i)
            {
                const auto& frame = stack.frames[i];

                if (frame.base != 0)
                    std::printf ("  %2d  %s+0x%llx\n", i, moduleName (frame.base),
                                 static_cast<unsigned long long> (frame.pc - frame.base));
                else
                    std::printf ("  %2d  0x%llx\n", i, static_cast<unsigned long long> (frame.pc));
            }
        }

        std::fflush (stdout);
    }

    /** Names for the stacks taken above (needs the PDB: CMakeLists.txt links
        EncoreTests with /DEBUG). Runs on its own thread: it needs the loader. */
    DWORD WINAPI printNames (LPVOID)
    {
        const auto process = GetCurrentProcess();

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

        if (SymInitialize (process, folder, TRUE) == FALSE)
        {
            std::printf ("\n(no names: SymInitialize failed, error %lu)\n", static_cast<unsigned long> (GetLastError()));
            return 1;
        }

        std::printf ("\nStacks with names:\n");

        for (int t = 0; t < threadCount; ++t)
        {
            const auto& stack = stacks[t];
            std::printf ("\nThread %lu%s:\n", static_cast<unsigned long> (stack.id), threadLabel (stack.id));

            for (int i = 0; i < stack.count; ++i)
            {
                // A return address points after the call: look up the call itself.
                const auto address = i == 0 ? stack.frames[i].pc : stack.frames[i].pc - 1;
                alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512] = {};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
                symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
                symbol->MaxNameLen = 511;
                DWORD64 displacement = 0;
                IMAGEHLP_LINE64 line {};
                line.SizeOfStruct = sizeof (line);
                DWORD lineDisplacement = 0;

                if (SymFromAddr (process, address, &displacement, symbol) == FALSE)
                    std::printf ("  %2d  %s+0x%llx\n", i, moduleName (stack.frames[i].base),
                                 static_cast<unsigned long long> (stack.frames[i].pc - stack.frames[i].base));
                else if (SymGetLineFromAddr64 (process, address, &lineDisplacement, &line) != FALSE)
                    std::printf ("  %2d  %s  (%s:%lu)\n", i, symbol->Name, line.FileName,
                                 static_cast<unsigned long> (line.LineNumber));
                else
                    std::printf ("  %2d  %s!%s + 0x%llx\n", i, moduleName (stack.frames[i].base), symbol->Name,
                                 static_cast<unsigned long long> (displacement));
            }
        }

        SymCleanup (process);
        std::fflush (stdout);
        return 0;
    }

    void printStacks()
    {
        listModules();
        captureStacks();
        printRaw();

        const auto helper = CreateThread (nullptr, 0, printNames, nullptr, 0, nullptr);

        if (helper == nullptr || WaitForSingleObject (helper, 30000) != WAIT_OBJECT_0)
            std::printf ("\n(no names: the symbol helper didn't finish in 30 s, so the Windows loader lock is"
                         " probably held. Name the offsets above with the uploaded EncoreTests symbols.)\n");

        if (helper != nullptr)
            CloseHandle (helper);
    }
   #else
    void printStacks()
    {
        std::printf ("(stacks are only printed on x64)\n");
    }
   #endif
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
