// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "app/EventLog.h"

#include "engine/blocks/PluginBlock.h"
#include "engine/core/Realtime.h"
#include "platform/CrashReport.h"

namespace encore::eventlog
{

namespace
{
    std::unique_ptr<juce::FileLogger> logger;
    juce::File folder;

    // Prepared up front: the crash handler should do as little as possible.
    std::string crashFilePrefix, pluginCrashFile;

    /** "the audio thread" etc., for the crash report. */
    juce::String threadName()
    {
        if (rt::isInRealtimeSection())
            return "the audio thread";

        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
            return "the message thread (windows, editing)";

        return "another thread (started by a plugin or a driver), id " + juce::String::toHexString ((juce::pointer_sized_int) juce::Thread::getCurrentThreadId());
    }

    void onCrash (void* exceptionPointers)
    {
        const auto facts = system::describeCrash (exceptionPointers);

        // Which plugin: the one Encore was calling on this thread, else every
        // block whose plugin the crashing code (or, deeper, its caller)
        // belongs to. Those blocks start switched off next time.
        juce::String plugins;

        if (const auto* label = PluginBlock::getPluginInUseOnThisThread())
            plugins = juce::String::fromUTF8 (label);

        if (plugins.isEmpty())
            plugins = PluginBlock::getCrashLabelsForModule (facts.module);

        for (int i = 0; plugins.isEmpty() && i < facts.stackModules.size(); ++i)
            plugins = PluginBlock::getCrashLabelsForModule (facts.stackModules[i]);

        if (plugins.isNotEmpty() && ! pluginCrashFile.empty())
            juce::File (juce::String (pluginCrashFile)).replaceWithText (plugins);

        juce::StringArray pluginNames;

        for (const auto& [blockId, name] : PluginBlock::parseCrashLabels (plugins))
            pluginNames.addIfNotAlreadyThere (name);

        const auto when = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        juce::String text;
        text << "Encore crashed at " << when << "\n\n";

        if (facts.what.isNotEmpty())
            text << "What:   " << facts.what << "\n";

        if (facts.module.isNotEmpty())
            text << "Where:  " << facts.module << " + " << facts.moduleOffset << "\n";

        text << "Thread: " << threadName() << "\n";

        if (! pluginNames.isEmpty())
            text << "Plugin: " << pluginNames.joinIntoString (", ") << " (its blocks start switched off next time)\n";

        if (! facts.stack.isEmpty())
            text << "\nStack, by file:\n" << facts.stack.joinIntoString ("\n") << "\n";

        text << "\nStack, by name (where known):\n" << juce::SystemStats::getStackBacktrace();

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
