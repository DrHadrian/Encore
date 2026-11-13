// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/PluginCatalog.h"

#include "engine/blocks/PluginBlock.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <stdlib.h>
#endif

namespace encore
{

namespace
{
    const juce::String scanArgument = "--scan-plugin";
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 256;

    void addFormats (juce::AudioPluginFormatManager& formats)
    {
        formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    }

    juce::String fileName (const juce::String& path)
    {
        // Either separator: a catalog may come from another system.
        return path.fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
    }
}

//==============================================================================
/** Scans the folders, one file at a time, each in its own scanner process. */
class PluginCatalog::ScanThread final : public juce::Thread
{
public:
    ScanThread (PluginCatalog& c, bool everything)
        : Thread ("Encore plugin scan"), catalog (c), rescanEverything (everything)
    {
        // Copies: the scan doesn't touch the catalog's settings while it runs.
        const juce::ScopedLock sl (catalog.lock);
        folders = catalog.folders;
        timeout = catalog.scanTimeout;
        scanner = catalog.scanner;

        // The log of the last scans only (a fresh one when it grows large).
        logFile = catalog.dataFile.getSiblingFile ("plugin-scan.log");

        if (logFile.getSize() > 512 * 1024)
            logFile.deleteFile();

        logFile.appendText ("\n" + juce::Time::getCurrentTime().toString (true, true, true, true)
                            + (everything ? "  Scan all again" : "  Scan") + " in " + folders.toString() + "\n");
    }

    ~ScanThread() override { stopThread (10000); }

    void run() override
    {
        auto& format = *catalog.formats.getFormat (0);
        auto files = format.searchPathsForPlugins (folders, true, false);
        files.sort (true);

        for (int i = 0; i < files.size(); ++i)
        {
            if (threadShouldExit())
            {
                catalog.scanFinished (files, true);
                return;
            }

            const auto& file = files[i];
            catalog.scanProgress (i, files.size(), file);

            if (! rescanEverything && catalog.isUpToDate (file, format))
                continue;

            if (! scanOne (file))
            {
                catalog.scanFinished (files, true);
                return;
            }
        }

        catalog.scanFinished (files, false);
    }

private:
    void fail (const juce::String& file, const juce::String& reason, juce::Time started, const juce::String& detail = {})
    {
        catalog.scanFailed (file, reason);
        log (file, "BLOCKED: " + reason + (detail.isNotEmpty() ? " (" + detail + ")" : juce::String()), started);
    }

    /** One line per scanned file in plugin-scan.log, next to the plugin list. */
    void log (const juce::String& file, const juce::String& what, juce::Time started)
    {
        const auto seconds = (juce::Time::getCurrentTime() - started).inSeconds();
        logFile.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + file + "  ("
                            + juce::String (seconds, 1) + " s)  " + what + "\n");
    }

    /** False if the scan was cancelled. */
    bool scanOne (const juce::String& file)
    {
        const auto result = juce::File::createTempFile (".xml");
        juce::ChildProcess process;

        if (! process.start (juce::StringArray { scanner.getFullPathName(), scanArgument, file, result.getFullPathName() }, 0))
        {
            catalog.scanFailed (file, "Encore couldn't start its plugin scanner");
            return true;
        }

        const auto started = juce::Time::getCurrentTime();
        const auto progress = result.withFileExtension ("progress");
        auto lastSignOfLife = started;
        juce::String lastProgress;

        while (process.isRunning())
        {
            if (threadShouldExit())
            {
                process.kill();
                result.deleteFile();
                progress.deleteFile();
                return false;
            }

            // Each plugin it finishes counts as a sign of life (shells hold many).
            if (const auto p = progress.loadFileAsString(); p != lastProgress)
            {
                lastProgress = p;
                lastSignOfLife = juce::Time::getCurrentTime();
            }

            if (juce::Time::getCurrentTime() - lastSignOfLife > timeout)
            {
                process.kill();
                result.deleteFile();
                progress.deleteFile();
                fail (file, "Didn't finish loading within " + juce::String (juce::roundToInt (timeout.inSeconds())) + " s while being scanned",
                      started);
                return true;
            }

            wait (20);
        }

        juce::OwnedArray<juce::PluginDescription> found;
        const auto xml = juce::parseXMLIfTagMatches (result, "ENCORE_SCAN");
        result.deleteFile();
        progress.deleteFile();

        if (xml == nullptr)
        {
            fail (file, "Crashed while being scanned", started,
                  "exit code " + juce::String::toHexString ((int) process.getExitCode()));
            return true;
        }

        juce::StringArray skipped;

        for (auto* e : xml->getChildIterator())
        {
            if (e->hasTagName ("SKIPPED"))
            {
                skipped.add (e->getStringAttribute ("name") + ": " + e->getStringAttribute ("reason"));
                continue;
            }

            auto description = std::make_unique<juce::PluginDescription>();

            if (description->loadFromXml (*e))
                found.add (description.release());
        }

        if (found.isEmpty())
        {
            // One plugin in the file: its own reason; several: the first, and how many.
            const auto reason = skipped.isEmpty() ? juce::String ("Has no plugin Encore can load")
                              : skipped.size() == 1 ? skipped[0].fromFirstOccurrenceOf (": ", false, false)
                                                    : skipped[0] + " (and " + juce::String (skipped.size() - 1) + " more)";
            fail (file, reason, started);
        }
        else
        {
            catalog.scanFoundTypes (file, found);
            log (file, "found " + juce::String (found.size()) + (skipped.isEmpty() ? juce::String() : ", skipped " + skipped.joinIntoString ("; ")),
                 started);
        }

        return true;
    }

    PluginCatalog& catalog;
    const bool rescanEverything;
    juce::FileSearchPath folders;
    juce::RelativeTime timeout;
    juce::File scanner, logFile;
};

//==============================================================================
PluginCatalog::PluginCatalog (juce::File dataFileToUse, juce::File scannerToUse)
    : dataFile (std::move (dataFileToUse)), scanner (std::move (scannerToUse))
{
    addFormats (formats);
    folders = getDefaultFolders();
    load();
}

PluginCatalog::~PluginCatalog()
{
    scanThread = nullptr;   // stops the scan (and kills a running scanner)
    cancelPendingUpdate();
}

juce::FileSearchPath PluginCatalog::getDefaultFolders()
{
    juce::VST3PluginFormat format;
    return format.getDefaultLocationsToSearch();
}

void PluginCatalog::setFolders (const juce::FileSearchPath& newFolders)
{
    {
        const juce::ScopedLock sl (lock);

        if (newFolders.toString() == folders.toString())
            return;

        folders = newFolders;
    }

    save();
    sendChangeMessage();
}

//==============================================================================
juce::Array<juce::PluginDescription> PluginCatalog::getPlugins() const
{
    juce::Array<juce::PluginDescription> usable;

    // Instruments without an audio input can't go in a guitar chain.
    for (const auto& d : known.getTypes())
        if (d.numInputChannels > 0)
            usable.add (d);

    std::sort (usable.begin(), usable.end(), [] (const auto& a, const auto& b)
    {
        const auto byName = a.name.compareNatural (b.name);
        return byName != 0 ? byName < 0 : a.manufacturerName.compareNatural (b.manufacturerName) < 0;
    });

    return usable;
}

std::vector<PluginCatalog::Blocked> PluginCatalog::getBlocked() const
{
    std::vector<Blocked> list;

    {
        const juce::ScopedLock sl (lock);

        for (const auto& [file, reason] : blocked)
            list.push_back ({ file, reason });
    }

    std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b)
    {
        return fileName (a.file).compareNatural (fileName (b.file)) < 0;
    });

    return list;
}

void PluginCatalog::unblock (const juce::String& file)
{
    {
        const juce::ScopedLock sl (lock);

        if (blocked.erase (file) == 0)
            return;
    }

    save();
    sendChangeMessage();
}

PluginRef PluginCatalog::toRef (const juce::PluginDescription& d)
{
    PluginRef ref;
    ref.format = d.pluginFormatName;
    ref.name = d.name;
    ref.maker = d.manufacturerName;
    ref.uid = d.uniqueId;
    ref.version = d.version;
    ref.file = d.fileOrIdentifier;
    return ref;
}

std::optional<juce::PluginDescription> PluginCatalog::find (const PluginRef& ref) const
{
    if (ref.isEmpty())
        return {};

    const auto types = known.getTypes();

    for (const auto& d : types)
        if (d.pluginFormatName == ref.format && d.uniqueId == ref.uid && d.name == ref.name)
            return d;

    // Same plugin under another ID (a different build): same name and maker.
    for (const auto& d : types)
        if (d.pluginFormatName == ref.format && d.name == ref.name && d.manufacturerName == ref.maker)
            return d;

    return {};
}

std::optional<PluginCatalog::Blocked> PluginCatalog::findBlocked (const PluginRef& ref) const
{
    const juce::ScopedLock sl (lock);

    for (const auto& [file, reason] : blocked)
        if (PluginBlock::vst3BundleOf (file).equalsIgnoreCase (PluginBlock::vst3BundleOf (ref.file)))
            return Blocked { file, reason };

    return {};
}

//==============================================================================
void PluginCatalog::startScan()     { startScanThread (false); }
void PluginCatalog::startFullScan() { startScanThread (true); }

void PluginCatalog::startScanThread (bool rescanEverything)
{
    if (isScanning())
        return;

    if (rescanEverything)
    {
        const juce::ScopedLock sl (lock);
        blocked.clear();
    }

    {
        const juce::ScopedLock sl (lock);
        scanStatus = "Looking for plugins...";
        progress = {};
    }

    scanThread = nullptr;   // a finished one not collected yet
    scanEnded.store (false);
    scanning.store (true);
    scanThread = std::make_unique<ScanThread> (*this, rescanEverything);
    scanThread->startThread();
    sendChangeMessage();
}

void PluginCatalog::cancelScan()
{
    if (scanThread != nullptr)
    {
        scanThread->signalThreadShouldExit();
    }
    else if (scanning.load())
    {
        // Only a test's shown progress (no thread): end it at once.
        scanFinished ({}, true);
    }
}

bool PluginCatalog::isScanning() const noexcept
{
    return scanning.load();
}

juce::String PluginCatalog::getScanStatus() const
{
    {
        const juce::ScopedLock sl (lock);

        if (scanStatus.isNotEmpty())
            return scanStatus;
    }

    return summary();
}

juce::String PluginCatalog::summary() const
{
    const auto plugins = getPlugins().size();
    const auto numBlocked = (int) getBlocked().size();
    return juce::String (plugins) + (plugins == 1 ? " plugin" : " plugins")
           + (numBlocked > 0 ? ", " + juce::String (numBlocked) + " blocked" : juce::String());
}

void PluginCatalog::setScanTimeout (juce::RelativeTime t)
{
    const juce::ScopedLock sl (lock);
    scanTimeout = t;
}

bool PluginCatalog::isUpToDate (const juce::String& file, juce::AudioPluginFormat& format) const
{
    {
        const juce::ScopedLock sl (lock);

        if (blocked.count (file) > 0)
            return true;   // stays blocked until unblocked or a full scan
    }

    // Its plugins, by the .vst3 they're in (JUCE may name the DLL inside).
    auto any = false;

    for (const auto& d : known.getTypes())
    {
        if (! isInFile (d, file))
            continue;

        if (format.pluginNeedsRescanning (d))
            return false;

        any = true;
    }

    return any;
}

bool PluginCatalog::isInFile (const juce::PluginDescription& d, const juce::String& file)
{
    return PluginBlock::vst3BundleOf (d.fileOrIdentifier).equalsIgnoreCase (PluginBlock::vst3BundleOf (file));
}

void PluginCatalog::scanProgress (int index, int total, const juce::String& file)
{
    {
        const juce::ScopedLock sl (lock);
        scanStatus = "Scanning " + juce::String (index + 1) + " of " + juce::String (total) + ": " + fileName (file);
        progress = { index + 1, total };
    }

    triggerAsyncUpdate();
}

PluginCatalog::ScanProgress PluginCatalog::getScanProgress() const
{
    const juce::ScopedLock sl (lock);
    return isScanning() ? progress : ScanProgress {};
}

void PluginCatalog::showScanProgressForTesting (int index, int total, const juce::String& file)
{
    jassert (scanThread == nullptr);
    scanning.store (true);
    scanProgress (index, total, file);
}

void PluginCatalog::scanFoundTypes (const juce::String& file, const juce::OwnedArray<juce::PluginDescription>& found)
{
    for (const auto& d : known.getTypes())
        if (isInFile (d, file))
            known.removeType (d);

    for (auto* d : found)
        known.addType (*d);

    const juce::ScopedLock sl (lock);
    blocked.erase (file);
}

void PluginCatalog::scanFailed (const juce::String& file, const juce::String& reason)
{
    for (const auto& d : known.getTypes())
        if (isInFile (d, file))
            known.removeType (d);

    const juce::ScopedLock sl (lock);
    blocked[file] = reason;
}

void PluginCatalog::scanFinished (const juce::StringArray& filesNowPresent, bool cancelled)
{
    if (! cancelled)
    {
        // Uninstalled plugins (or folders no longer searched) leave the list.
        for (const auto& d : known.getTypes())
            if (std::none_of (filesNowPresent.begin(), filesNowPresent.end(), [&d] (const juce::String& f) { return isInFile (d, f); }))
                known.removeType (d);

        const juce::ScopedLock sl (lock);

        for (auto it = blocked.begin(); it != blocked.end();)
            it = filesNowPresent.contains (it->first) ? std::next (it) : blocked.erase (it);
    }

    {
        const juce::ScopedLock sl (lock);
        scanStatus = cancelled ? juce::String ("Scan stopped") : juce::String();   // empty: the summary
    }

    scanning.store (false);
    scanEnded.store (true);
    triggerAsyncUpdate();
}

void PluginCatalog::handleAsyncUpdate()
{
    // The scan reported its end: the thread is returning (joining it is quick).
    if (scanEnded.exchange (false))
    {
        scanThread = nullptr;
        save();
    }

    sendChangeMessage();
}

//==============================================================================
void PluginCatalog::load()
{
    const auto xml = juce::parseXMLIfTagMatches (dataFile, "ENCORE_PLUGINS");

    if (xml == nullptr)
        return;

    if (auto* f = xml->getChildByName ("FOLDERS"))
        folders = juce::FileSearchPath (f->getStringAttribute ("paths"));

    if (auto* k = xml->getChildByName ("KNOWNPLUGINS"))
        known.recreateFromXml (*k);

    if (auto* b = xml->getChildByName ("BLOCKED"))
        for (auto* e : b->getChildWithTagNameIterator ("FILE"))
            blocked[e->getStringAttribute ("path")] = e->getStringAttribute ("reason");
}

void PluginCatalog::save() const
{
    juce::XmlElement root ("ENCORE_PLUGINS");

    {
        const juce::ScopedLock sl (lock);
        root.createNewChildElement ("FOLDERS")->setAttribute ("paths", folders.toString());

        auto* b = root.createNewChildElement ("BLOCKED");

        for (const auto& [file, reason] : blocked)
        {
            auto* e = b->createNewChildElement ("FILE");
            e->setAttribute ("path", file);
            e->setAttribute ("reason", reason);
        }
    }

    if (auto k = known.createXml())
        root.addChildElement (k.release());

    dataFile.getParentDirectory().createDirectory();

    // Writes a temporary file, then swaps it in. If that fails, the list is
    // found again by the next scan.
    const auto saved = root.writeTo (dataFile);
    jassert (saved);
    juce::ignoreUnused (saved);
}

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> PluginCatalog::createInstance (const juce::PluginDescription& description,
                                                                          double sampleRate, int blockSize,
                                                                          juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto& maker = instanceMakerForTesting(); maker != nullptr)
        if (auto instance = maker (description))
            return instance;

    return formats.createPluginInstance (description, sampleRate, blockSize, error);
}

std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&)>& PluginCatalog::instanceMakerForTesting()
{
    static std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&)> maker;
    return maker;
}

//==============================================================================
bool PluginCatalog::isScannerCommandLine (const juce::StringArray& args)
{
    return args.size() >= 3 && args.contains (scanArgument);
}

namespace
{
    /** The scanner process's work, driven by the message loop: each plugin
        is created, given time to start (plugins post messages while they
        start), played briefly, closed, and given time to finish closing,
        the way a host treats them. Tearing a plugin down at once can make it
        crash on the way out (and some then report a crash on their next
        start). After each plugin, a progress file tells the scan it is
        still alive (a shell with hundreds of plugins takes a while). */
    class ScannerProcess final : private juce::Timer
    {
    public:
        ScannerProcess (juce::String fileToScan, juce::File resultToWrite, std::function<void (int)> onDone)
            : file (std::move (fileToScan)), result (std::move (resultToWrite)), done (std::move (onDone))
        {
            addFormats (formats);
            root.setAttribute ("file", file);

            for (auto* format : formats.getFormats())
                if (format->fileMightContainThisPluginType (file))
                    format->findAllTypesForFile (types, file);

            startTimer (1);
        }

        ~ScannerProcess() override { stopTimer(); }

    private:
        static constexpr int settleMs = 250;

        void timerCallback() override
        {
            stopTimer();

            if (instance != nullptr)
            {
                if (! played)
                {
                    play();
                    played = true;
                    startTimer (settleMs);   // let it finish closing
                }

                return;
            }

            if (next >= types.size())
            {
                finish();
                return;
            }

            auto& d = *types[next++];

            // Encore can't use instruments: not even loaded (some, like a
            // whole rack, are the heaviest thing in their file).
            if (d.isInstrument)
            {
                skip (d, "Is an instrument (no audio input)");
                startTimer (1);
                return;
            }

            juce::String error;
            instance = formats.createPluginInstance (d, testSampleRate, testBlockSize, error);
            played = false;

            if (instance == nullptr)
            {
                skip (d, "Couldn't be loaded" + (error.isNotEmpty() ? ": " + error : juce::String()));
                startTimer (1);
                return;
            }

            instance->setPlayHead (&playHead);
            PluginBlock::setUp (*instance, {});
            startTimer (settleMs);   // let it finish starting
        }

        void play()
        {
            auto& d = *types[next - 1];
            instance->prepareToPlay (testSampleRate, testBlockSize);

            juce::AudioBuffer<float> buffer (juce::jmax (1, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels()),
                                             testBlockSize);
            juce::MidiBuffer midi;

            for (int i = 0; i < 4; ++i)
            {
                buffer.clear();
                instance->processBlock (buffer, midi);
            }

            juce::MemoryBlock state;
            instance->getStateInformation (state);
            instance->releaseResources();

            d.numInputChannels = instance->getMainBusNumInputChannels();
            d.numOutputChannels = instance->getMainBusNumOutputChannels();
            instance = nullptr;

            if (d.numInputChannels == 0)
                skip (d, d.isInstrument ? "Is an instrument (no audio input)" : "Has no audio input Encore can use");
            else if (d.numOutputChannels == 0)
                skip (d, "Has no audio output Encore can use");
            else
                root.addChildElement (d.createXml().release());

            result.withFileExtension ("progress").replaceWithText (juce::String (next));
        }

        void skip (const juce::PluginDescription& d, const juce::String& reason)
        {
            auto* e = root.createNewChildElement ("SKIPPED");
            e->setAttribute ("name", d.name);
            e->setAttribute ("reason", reason);
        }

        void finish()
        {
            // Only a complete result file counts as a successful scan.
            const auto ok = root.writeTo (result);
            result.withFileExtension ("progress").deleteFile();

            if (done != nullptr)
                done (ok ? 0 : 1);
        }

        const juce::String file;
        const juce::File result;
        std::function<void (int)> done;
        juce::AudioPluginFormatManager formats;
        juce::OwnedArray<juce::PluginDescription> types;
        juce::XmlElement root { "ENCORE_SCAN" };
        HostPlayHead playHead;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        int next = 0;
        bool played = false;
    };

    std::unique_ptr<ScannerProcess> scannerProcess;
}

void PluginCatalog::startScanner (const juce::StringArray& args, std::function<void (int)> onDone)
{
   #if JUCE_WINDOWS
    // A plugin that crashes here must not leave a "stopped working" dialog
    // up: the scan would wait for it until it times out.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif

    const auto at = args.indexOf (scanArgument);

    if (at < 0 || at + 2 >= args.size())
    {
        onDone (2);
        return;
    }

    scannerProcess = std::make_unique<ScannerProcess> (args[at + 1], juce::File (args[at + 2].unquoted()),
                                                       [onDone] (int code)
    {
        // Finished: free the scanner (and its plugins) after this callback.
        juce::MessageManager::callAsync ([onDone, code]
        {
            scannerProcess = nullptr;
            onDone (code);
        });
    });
}

#if JUCE_MODAL_LOOPS_PERMITTED
int PluginCatalog::runScanner (const juce::StringArray& args)
{
    auto exitCode = -1;
    startScanner (args, [&exitCode] (int code) { exitCode = code; });

    while (exitCode < 0)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    return exitCode;
}
#endif

} // namespace encore
