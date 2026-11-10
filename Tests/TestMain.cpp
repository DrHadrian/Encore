// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <iostream>

int main()
{
    // Creates the message manager and shuts it (and other singletons) down cleanly.
    const juce::ScopedJuceInitialiser_GUI juceRuntime;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("Encore");

    int failures = 0, passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::cout << "\nEncore tests: " << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
