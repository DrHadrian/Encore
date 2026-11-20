// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// Encore's threads are joined, never killed (decision 0060). A thread killed
// while it holds the Windows loader lock leaves it held for good: every thread
// start, thread exit and DLL load after it hangs. That was main's CI hang.

#include "engine/core/BackgroundJobs.h"
#include "engine/core/ThreadStop.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace encore::test
{

class ThreadStopTests final : public juce::UnitTest
{
    /** The text with comments blanked out; line breaks kept, so line numbers stay. */
    static juce::String withoutComments (const juce::String& text)
    {
        juce::String out;
        out.preallocateBytes (text.getNumBytesAsUTF8());
        bool block = false, line = false;

        for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
        {
            const auto c = *p, next = *(p + 1);

            if (block)
            {
                if (c == '*' && next == '/') { block = false; ++p; }
                else if (c == '\n') out << '\n';
            }
            else if (line)
            {
                if (c == '\n') { line = false; out << '\n'; }
            }
            else if (c == '/' && next == '*') { block = true; ++p; }
            else if (c == '/' && next == '/') { line = true; ++p; }
            else out << juce::String::charToString (c);
        }

        return out;
    }

    /** The name as a whole word (stopThreadWithoutKilling isn't stopThread). */
    static bool hasWord (const juce::String& code, const juce::String& name)
    {
        for (int at = code.indexOf (name); at >= 0; at = code.indexOf (at + 1, name))
        {
            const auto end = at + name.length();
            const juce::juce_wchar before = at > 0 ? code[at - 1] : (juce::juce_wchar) ' ';
            const juce::juce_wchar after = end < code.length() ? code[end] : (juce::juce_wchar) ' ';

            if (! (juce::CharacterFunctions::isLetterOrDigit (before) || before == '_')
                && ! (juce::CharacterFunctions::isLetterOrDigit (after) || after == '_'))
                return true;
        }

        return false;
    }

public:
    ThreadStopTests() : juce::UnitTest ("Threads are never killed", "Encore") {}

    void runTest() override
    {
        beginTest ("The loader pool finishes a running job, drops waiting ones, and has stopped when it's gone");
        {
            std::atomic<bool> started { false }, finished { false };
            std::atomic<int> later { 0 };
            juce::WaitableEvent release (true);   // manual reset: it releases both jobs

            {
                BackgroundJobs jobs;

                // Both threads busy, then two more jobs that never start.
                jobs.add ([&] { started = true; release.wait (-1); juce::Thread::sleep (700); finished = true; });
                jobs.add ([&] { release.wait (-1); });
                jobs.add ([&] { ++later; });
                jobs.add ([&] { ++later; });

                for (int i = 0; i < 1000 && ! started; ++i)
                    juce::Thread::sleep (2);

                expect (started.load());
                release.signal();
                // Leaving the scope stops the pool while a job still has 700 ms
                // to go: more than juce::ThreadPool's 500 ms before it killed.
            }

            expect (finished.load(), "the running job finished: it wasn't killed");
            expectEquals (later.load(), 0, "jobs that hadn't started are dropped");
        }

        beginTest ("A juce::Thread is stopped by waiting, however long it takes");
        {
            struct Slow final : juce::Thread
            {
                Slow() : juce::Thread ("Encore test: slow to stop") {}
                void run() override
                {
                    while (! threadShouldExit())
                        wait (-1);

                    juce::Thread::sleep (1200);   // longer than any limit Encore used before (1000 ms)
                    done = true;
                }
                std::atomic<bool> done { false };
            } slow;

            slow.startThread();
            juce::Thread::sleep (20);
            stopThreadWithoutKilling (slow);
            expect (slow.done.load(), "it ran to the end");
            expect (! slow.isThreadRunning());
        }

        beginTest ("No code in Source/ can kill a thread (stopThread with a limit, killThread, juce::ThreadPool)");
        {
            const juce::File source (ENCORE_SOURCE_ROOT "/Source");
            expect (source.isDirectory(), source.getFullPathName());

            juce::StringArray found;

            for (const auto& entry : juce::RangedDirectoryIterator (source, true, "*.cpp;*.h"))
            {
                const auto lines = juce::StringArray::fromLines (withoutComments (entry.getFile().loadFileAsString()));

                for (int i = 0; i < lines.size(); ++i)
                {
                    const auto& code = lines[i];

                    if (hasWord (code, "stopThread") || hasWord (code, "killThread") || hasWord (code, "ThreadPool"))
                        found.add (entry.getFile().getRelativePathFrom (source) + ":" + juce::String (i + 1)
                                   + " " + code.trim());
                }
            }

            expect (found.isEmpty(), "use stopThreadWithoutKilling (ThreadStop.h) or BackgroundJobs: "
                                         + found.joinIntoString ("; "));
        }
    }
};

static ThreadStopTests threadStopTests;

} // namespace encore::test
