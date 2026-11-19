// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "model/ShowModel.h"

namespace encore
{

/** The VST3 plugins this computer has, and how to make them.

    Scanning: Encore never loads an unknown plugin into itself. Each new or
    changed plugin file is examined by a separate copy of Encore started with
    `--scan-plugin <file> <result file>` (see runScanner). If that copy
    crashes or doesn't finish in time, the file goes on the blocklist with the
    reason, and Encore carries on. Known files that haven't changed aren't
    scanned again.

    What it found, the blocklist and the folders to search are kept per
    computer (decision 0006), in one XML file in the app-data folder.

    Threads: everything is message thread, except createInstance (any thread)
    and the scan itself, which runs on its own thread and reports back with
    change messages.
*/
class PluginCatalog final : public juce::ChangeBroadcaster,
                            private juce::AsyncUpdater
{
public:
    /** dataFile: where the list is kept. scanner: the program to start for
        scanning one file (Encore itself; the tests use the test runner). */
    PluginCatalog (juce::File dataFile, juce::File scanner);
    ~PluginCatalog() override;

    //== Folders ===============================================================
    juce::FileSearchPath getFolders() const { return folders; }
    void setFolders (const juce::FileSearchPath&);

    /** Where VST3 plugins are installed by default (Common Files\VST3 on Windows). */
    static juce::FileSearchPath getDefaultFolders();

    //== What was found ========================================================
    /** Every usable plugin, sorted by name. */
    juce::Array<juce::PluginDescription> getPlugins() const;

    struct Blocked
    {
        juce::String file;
        juce::String reason;    // "Crashed while being scanned"
    };

    /** Files that failed the scan, sorted by file name. */
    std::vector<Blocked> getBlocked() const;

    /** Takes a file off the blocklist; the next scan tries it again. */
    void unblock (const juce::String& file);

    /** Turns a scanned plugin into what a show saves. */
    static PluginRef toRef (const juce::PluginDescription&);

    /** The installed plugin a show's block asks for: the same plugin (format,
        unique ID and name), else one with the same name and maker. */
    std::optional<juce::PluginDescription> find (const PluginRef&) const;

    /** Whether the file of a plugin a show asks for is on the blocklist. */
    std::optional<Blocked> findBlocked (const PluginRef&) const;

    //== Scanning (message thread) =============================================
    /** Looks for new, changed and removed plugins in the folders. Already
        known, unchanged files aren't loaded again. */
    void startScan();

    /** Like startScan, but scans every file again (and retries the blocklist). */
    void startFullScan();

    void cancelScan();
    bool isScanning() const noexcept;

    /** "Scanning 3 of 40: Shimmer.vst3", "Scan stopped", or "12 plugins, 1 blocked". */
    juce::String getScanStatus() const;

    /** How long a scanned file may go without finishing a plugin (default 60 s). */
    void setScanTimeout (juce::RelativeTime);

    //== Making plugins =======================================================
    /** Message thread: creates the plugin (not yet set up or prepared). */
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&,
                                                               double sampleRate, int blockSize,
                                                               juce::String& error);

    //== The scanner process ===================================================
    /** True if Encore was started as a scanner (by a scan, not by a person). */
    static bool isScannerCommandLine (const juce::StringArray& args);

    /** The scanner process's work: loads and plays every plugin in the file
        (driven by the message loop, so plugins get time to start and close),
        writes what it found and what it skipped (with the reason) to the
        result file, then calls onDone with the exit code. */
    static void startScanner (const juce::StringArray& args, std::function<void (int exitCode)> onDone);

   #if JUCE_MODAL_LOOPS_PERMITTED
    /** startScanner, waiting for it (the test runner). */
    static int runScanner (const juce::StringArray& args);
   #endif

private:
    class ScanThread;

    void handleAsyncUpdate() override;
    void load();
    void save() const;
    juce::String summary() const;
    void startScanThread (bool rescanEverything);

    // What the scan thread asks and reports.
    bool isUpToDate (const juce::String& file, juce::AudioPluginFormat&) const;
    static bool isInFile (const juce::PluginDescription&, const juce::String& file);
    void scanProgress (int index, int total, const juce::String& file);
    void scanFoundTypes (const juce::String& file, const juce::OwnedArray<juce::PluginDescription>&);
    void scanFailed (const juce::String& file, const juce::String& reason);
    void scanFinished (const juce::StringArray& filesNowPresent, bool cancelled);

    const juce::File dataFile, scanner;
    juce::AudioPluginFormatManager formats;
    juce::FileSearchPath folders;

    mutable juce::CriticalSection lock;
    juce::KnownPluginList known;                     // internally locked too
    std::map<juce::String, juce::String> blocked;    // file -> reason
    juce::String scanStatus;
    juce::RelativeTime scanTimeout { juce::RelativeTime::seconds (60) };

    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false }, scanEnded { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};

} // namespace encore
