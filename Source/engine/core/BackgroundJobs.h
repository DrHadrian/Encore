// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace encore
{

/** Shared worker threads for slow, non-real-time work: loading amp models and
    IRs, later show files. Get it with juce::SharedResourcePointer<BackgroundJobs>;
    all users share one pool, which stops when the last user goes away.

    Its threads are always joined, never killed (decision 0060). This used to be
    a juce::ThreadPool, which gives a thread 500 ms to stop and then calls
    TerminateThread on Windows. A thread that is ending holds the Windows loader
    lock while the DLLs are told; killed then, the lock stays held for good, and
    every thread start, thread exit and DLL load after it hangs (CI, Nov 2026).
    Jobs still waiting are dropped when the pool stops; a running job finishes
    (plugin creation gives up within 50 ms once its show is closed).
*/
class BackgroundJobs
{
public:
    BackgroundJobs()
    {
        for (auto& thread : threads)
            thread = std::thread ([this] { run(); });
    }

    ~BackgroundJobs()
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            stopping = true;
            queue.clear();
        }

        wake.notify_all();

        for (auto& thread : threads)
            thread.join();
    }

    void add (std::function<void()> job)
    {
        {
            const std::lock_guard<std::mutex> l (lock);

            if (stopping)
                return;

            queue.push_back (std::move (job));
        }

        wake.notify_one();
    }

private:
    void run()
    {
        juce::Thread::setCurrentThreadName ("Encore loader");

        for (;;)
        {
            std::function<void()> job;

            {
                std::unique_lock<std::mutex> l (lock);
                wake.wait (l, [this] { return stopping || ! queue.empty(); });

                if (stopping)
                    return;

                job = std::move (queue.front());
                queue.pop_front();
            }

            job();
        }
    }

    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::function<void()>> queue;
    bool stopping = false;
    std::thread threads[2];   // last: they start once the rest is set up

    JUCE_DECLARE_NON_COPYABLE (BackgroundJobs)
};

} // namespace encore
