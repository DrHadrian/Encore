// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/EventLog.h"

#include "engine/blocks/PluginBlock.h"

namespace encore::eventlog
{

namespace
{
    std::unique_ptr<juce::FileLogger> logger;
    juce::File folder;

    // Prepared up front: the crash handler should do as little as possible.
    std::string crashFilePrefix, pluginCrashFile;

    void onCrash (void*)
    {
        // Crashed inside a plugin? Say which, so it starts switched off next time.
        if (const auto* plugin = PluginBlock::getPluginInUseOnThisThread(); plugin != nullptr && ! pluginCrashFile.empty())
            juce::File (juce::String (pluginCrashFile)).replaceWithText (juce::String::fromUTF8 (plugin));

        const auto when = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        const auto text = "Encore crashed at " + when + "\n\n" + juce::SystemStats::getStackBacktrace();

        juce::File (juce::String (crashFilePrefix) + when + ".txt").replaceWithText (text);

        if (logger != nullptr)
            logger->logMessage ("CRASH - see crash-" + when + ".txt\n" + text);
    }

    void deleteOldLogs()
    {
        auto logs = folder.findChildFiles (juce::File::findFiles, false, "Encore_*.log");

        // Oldest first by name (they're named by date and time).
        std::sort (logs.begin(), logs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

        for (int i = 0; i < logs.size() - 30; ++i)
            logs.getReference (i).deleteFile();
    }
}

void start (const juce::File& logsFolder, const juce::String& appVersion)
{
    folder = logsFolder;
    folder.createDirectory();
    deleteOldLogs();

    const auto file = folder.getChildFile ("Encore_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S") + ".log");
    logger = std::make_unique<juce::FileLogger> (file, "Encore " + appVersion + " on " + juce::SystemStats::getOperatingSystemName()
                                                         + ", " + juce::SystemStats::getCpuModel()
                                                         + ", " + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB RAM",
                                                 0);
    juce::Logger::setCurrentLogger (logger.get());

    crashFilePrefix = folder.getChildFile ("crash-").getFullPathName().toStdString();
    pluginCrashFile = getPluginCrashFile().getFullPathName().toStdString();
    juce::SystemStats::setApplicationCrashHandler (onCrash);
}

void stop()
{
    write ("Encore closed normally");
    juce::Logger::setCurrentLogger (nullptr);
    logger = nullptr;
}

void write (const juce::String& message)
{
    juce::Logger::writeToLog (juce::Time::getCurrentTime().formatted ("%H:%M:%S  ") + message);
}

juce::File getLogsFolder() { return folder; }

juce::File getPluginCrashFile() { return folder.getParentDirectory().getChildFile ("plugin-crash.txt"); }

} // namespace encore::eventlog
