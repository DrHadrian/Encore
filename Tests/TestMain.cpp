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

    // JUCE logs to the debugger on Windows: print each step to the console
    // (flushed), so a crash in CI shows where it happened.
    struct ConsoleRunner final : juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    // ENCORE_TEST_ONLY=<name>|<name>...: run just those (e.g. the showcase tour).
    // ENCORE_TEST_SKIP=<name>|<name>...: run all but those. ctest uses both to
    // run the tests as several processes in parallel (CMakeLists.txt).
    const auto names = [] (const char* variable)
    {
        return juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable (variable, {}), "|", {});
    };

    const auto only = names ("ENCORE_TEST_ONLY");
    const auto skip = names ("ENCORE_TEST_SKIP");
    juce::Array<juce::UnitTest*> chosen;

    for (auto* test : juce::UnitTest::getTestsInCategory ("Encore"))
        if ((only.isEmpty() || only.contains (test->getName())) && ! skip.contains (test->getName()))
            chosen.add (test);

    runner.runTests (chosen);

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
