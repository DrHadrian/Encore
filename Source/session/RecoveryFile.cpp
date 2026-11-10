// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/RecoveryFile.h"

namespace encore
{

RecoveryFile::RecoveryFile (juce::File f) : file (std::move (f)) {}

std::optional<RecoveryFile::State> RecoveryFile::readUncleanExit() const
{
    if (! file.existsAsFile())
        return std::nullopt;

    const auto v = juce::JSON::parse (file.loadFileAsString());

    if (! v.isObject())
        return State {};   // unreadable, but its presence still means an unclean exit

    State s;
    s.showFolder = v.getProperty ("show", {}).toString();
    s.cue = (int) v.getProperty ("cue", -1);
    s.live = (bool) v.getProperty ("live", false);
    s.stageView = (bool) v.getProperty ("stage", false);
    s.recoveriesInARow = (int) v.getProperty ("recoveries", 0);
    s.lastRecovery = juce::Time ((juce::int64) v.getProperty ("lastRecovery", 0));
    return s;
}

void RecoveryFile::update (const State& state)
{
    if (written && *written == state)
        return;

    auto* o = new juce::DynamicObject();
    o->setProperty ("show", state.showFolder);
    o->setProperty ("cue", state.cue);
    o->setProperty ("live", state.live);
    o->setProperty ("stage", state.stageView);
    o->setProperty ("recoveries", state.recoveriesInARow);
    o->setProperty ("lastRecovery", state.lastRecovery.toMilliseconds());

    // Write next to it, then swap in: never a half-written file.
    file.getParentDirectory().createDirectory();
    const juce::TemporaryFile temp (file);

    if (temp.getFile().replaceWithText (juce::JSON::toString (juce::var (o))) && temp.overwriteTargetFileWithTemporary())
        written = state;
}

void RecoveryFile::markCleanExit()
{
    file.deleteFile();
    written.reset();
}

bool RecoveryFile::shouldGoLiveAutomatically (const State& crashed, juce::Time now)
{
    if (! crashed.live)
        return false;

    const auto recentRecovery = crashed.lastRecovery.toMilliseconds() > 0
                             && now - crashed.lastRecovery < juce::RelativeTime::minutes (10);
    return ! (recentRecovery && crashed.recoveriesInARow >= 2);
}

} // namespace encore
