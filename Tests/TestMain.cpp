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
        const auto* result = runner.getResult (i);
        failures += result->failures;
        passes += result->passes;

        // JUCE logs to the debugger on Windows: print failures where CI sees them.
        if (result->failures > 0)
        {
            std::cout << "FAILED: " << result->unitTestName << " / " << result->subcategoryName << "\n";

            for (const auto& message : result->messages)
                std::cout << "    " << message << "\n";
        }
    }

    std::cout << "\nEncore tests: " << passes << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
