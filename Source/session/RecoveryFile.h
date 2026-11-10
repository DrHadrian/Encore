// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace encore
{

/** Crash recovery. While Encore runs it keeps a small file saying where the
    show is (show, cue, live or muted, which view). A clean exit removes it.
    If it's still there at start-up, Encore didn't exit cleanly (crash, power
    cut, killed): it goes back to the same place.

    Written on the message thread, at most a few times a second, atomically
    (a crash mid-write leaves the previous version).
*/
class RecoveryFile
{
public:
    struct State
    {
        juce::String showFolder;
        int cue = -1;
        bool live = false;               // unmuted
        bool stageView = false;
        int recoveriesInARow = 0;        // crash loops: stop going live automatically
        juce::Time lastRecovery;

        bool operator== (const State&) const = default;
    };

    explicit RecoveryFile (juce::File file);

    /** At start-up: the state left by a run that didn't exit cleanly, if any. */
    std::optional<State> readUncleanExit() const;

    /** While running: write when something changed. */
    void update (const State&);

    /** On a clean exit. */
    void markCleanExit();

    /** Whether a recovery should go live by itself: not if Encore keeps
        crashing (a third time within ten minutes means something is wrong). */
    static bool shouldGoLiveAutomatically (const State& crashed, juce::Time now);

private:
    juce::File file;
    std::optional<State> written;
};

} // namespace encore
