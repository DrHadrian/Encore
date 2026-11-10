// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace encore
{

/** Shared worker threads for slow, non-real-time work: loading amp models and
    IRs, later show files. Get it with juce::SharedResourcePointer<BackgroundJobs>;
    all users share one pool, which stops when the last user goes away.
*/
class BackgroundJobs
{
public:
    BackgroundJobs()
        : pool (juce::ThreadPoolOptions{}
                    .withThreadName ("Encore loader")
                    .withNumberOfThreads (2))
    {
    }

    ~BackgroundJobs()
    {
        pool.removeAllJobs (true, 10000);
    }

    void add (std::function<void()> job)
    {
        pool.addJob (std::move (job));
    }

private:
    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE (BackgroundJobs)
};

} // namespace encore
