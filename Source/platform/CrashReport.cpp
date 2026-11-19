// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "platform/CrashReport.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace encore::system
{

#if JUCE_WINDOWS
namespace
{
    HMODULE moduleHandle (const void* address)
    {
        HMODULE module = nullptr;
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR> (address), &module);
        return module;
    }

    juce::String offsetInto (HMODULE module, const void* address)
    {
        return "0x" + juce::String::toHexString ((juce::int64) ((const char*) address - (const char*) module));
    }

    juce::String describeException (const EXCEPTION_RECORD& r)
    {
        const auto code = (juce::uint32) r.ExceptionCode;
        const auto hex = " (0x" + juce::String::toHexString ((juce::int64) code).toUpperCase() + ")";

        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
            {
                const auto kind = r.NumberParameters >= 2 ? r.ExceptionInformation[0] : 0;
                const auto verb = kind == 1 ? "writing" : kind == 8 ? "executing" : "reading";
                const auto at = r.NumberParameters >= 2 ? juce::String::toHexString ((juce::int64) r.ExceptionInformation[1]) : juce::String ("?");
                return juce::String ("Access violation ") + verb + " address 0x" + at + hex;
            }

            case EXCEPTION_STACK_OVERFLOW:        return "Stack overflow" + hex;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "Integer division by zero" + hex;
            case EXCEPTION_ILLEGAL_INSTRUCTION:   return "Illegal instruction" + hex;
            case EXCEPTION_PRIV_INSTRUCTION:      return "Privileged instruction" + hex;
            case EXCEPTION_IN_PAGE_ERROR:         return "Couldn't read code or data from disk" + hex;
            case EXCEPTION_BREAKPOINT:            return "Breakpoint" + hex;
            case 0xC0000409:                      return "Stack buffer overrun or fail-fast" + hex;
            case 0xE06D7363:                      return "C++ exception nobody caught" + hex;
            default:                              return "Exception" + hex;
        }
    }
}

juce::String moduleForAddress (const void* address)
{
    if (auto* module = moduleHandle (address))
    {
        wchar_t path[MAX_PATH * 2] = {};

        if (GetModuleFileNameW (module, path, (DWORD) std::size (path)) > 0)
            return juce::String (path);
    }

    return {};
}

CrashFacts describeCrash (void* exceptionPointers)
{
    CrashFacts facts;

    if (const auto* ep = static_cast<const EXCEPTION_POINTERS*> (exceptionPointers); ep != nullptr && ep->ExceptionRecord != nullptr)
    {
        const auto& record = *ep->ExceptionRecord;
        facts.what = describeException (record);
        facts.module = moduleForAddress (record.ExceptionAddress);

        if (auto* module = moduleHandle (record.ExceptionAddress))
            facts.moduleOffset = offsetInto (module, record.ExceptionAddress);
    }

    // Every frame by file and offset: symbols are often missing (plugins,
    // a Release Encore without its .pdb), the file never is.
    void* frames[128] = {};
    const auto count = (int) CaptureStackBackTrace (0, (DWORD) std::size (frames), frames, nullptr);

    for (int i = 0; i < count; ++i)
    {
        const auto file = moduleForAddress (frames[i]);
        auto* module = moduleHandle (frames[i]);
        facts.stackModules.add (file);
        facts.stack.add (juce::String (i) + ": " + (file.isNotEmpty() ? juce::File (file).getFileName() : juce::String ("?"))
                         + (module != nullptr ? " + " + offsetInto (module, frames[i]) : juce::String()));
    }

    return facts;
}

#else

juce::String moduleForAddress (const void*) { return {}; }

CrashFacts describeCrash (void*) { return {}; }

#endif

} // namespace encore::system
