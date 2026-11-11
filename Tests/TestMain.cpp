// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <iostream>

#include "session/PluginCatalog.h"

int main (int argc, char* argv[])
{
    // Creates the message manager and shuts it (and other singletons) down cleanly.
    const juce::ScopedJuceInitialiser_GUI juceRuntime;

    // The plugin tests start this program as the plugin scanner, like Encore.
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (encore::PluginCatalog::isScannerCommandLine (args))
        return encore::PluginCatalog::runScanner (args);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    // ENCORE_TEST_ONLY=<test name>: run just that one (e.g. the showcase tour).
    if (const auto only = juce::SystemStats::getEnvironmentVariable ("ENCORE_TEST_ONLY", {}); only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;

        for (auto* test : juce::UnitTest::getTestsInCategory ("Encore"))
            if (test->getName() == only)
                chosen.add (test);

        runner.runTests (chosen);
    }
    else
    {
        runner.runTestsInCategory ("Encore");
    }

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
