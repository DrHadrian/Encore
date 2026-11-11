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
        return juce::File::createFileWithoutCheckingPath (path).getFileName();
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

        while (process.isRunning())
        {
            if (threadShouldExit())
            {
                process.kill();
                result.deleteFile();
                return false;
            }

            if (juce::Time::getCurrentTime() - started > timeout)
            {
                process.kill();
                result.deleteFile();
                catalog.scanFailed (file, "Didn't finish loading within "
                                              + juce::String (juce::roundToInt (timeout.inSeconds())) + " s while being scanned");
                return true;
            }

            wait (20);
        }

        juce::OwnedArray<juce::PluginDescription> found;
        const auto xml = juce::parseXMLIfTagMatches (result, "ENCORE_SCAN");
        result.deleteFile();

        if (xml == nullptr)
        {
            catalog.scanFailed (file, "Crashed while being scanned");
            return true;
        }

        for (auto* e : xml->getChildIterator())
        {
            auto description = std::make_unique<juce::PluginDescription>();

            if (description->loadFromXml (*e))
                found.add (description.release());
        }

        if (found.isEmpty())
            catalog.scanFailed (file, "Has no plugin Encore can load");
        else
            catalog.scanFoundTypes (file, found);

        return true;
    }

    PluginCatalog& catalog;
    const bool rescanEverything;
    juce::FileSearchPath folders;
    juce::RelativeTime timeout;
    juce::File scanner;
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

    if (auto it = blocked.find (ref.file); it != blocked.end())
        return Blocked { it->first, it->second };

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
        scanThread->signalThreadShouldExit();
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

    return known.getTypeForFile (file) != nullptr && known.isListingUpToDate (file, format);
}

void PluginCatalog::scanProgress (int index, int total, const juce::String& file)
{
    {
        const juce::ScopedLock sl (lock);
        scanStatus = "Scanning " + juce::String (index + 1) + " of " + juce::String (total) + ": " + fileName (file);
    }

    triggerAsyncUpdate();
}

void PluginCatalog::scanFoundTypes (const juce::String& file, const juce::OwnedArray<juce::PluginDescription>& found)
{
    for (const auto& d : known.getTypes())
        if (d.fileOrIdentifier == file)
            known.removeType (d);

    for (auto* d : found)
        known.addType (*d);

    const juce::ScopedLock sl (lock);
    blocked.erase (file);
}

void PluginCatalog::scanFailed (const juce::String& file, const juce::String& reason)
{
    for (const auto& d : known.getTypes())
        if (d.fileOrIdentifier == file)
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
            if (! filesNowPresent.contains (d.fileOrIdentifier))
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
    return formats.createPluginInstance (description, sampleRate, blockSize, error);
}

//==============================================================================
bool PluginCatalog::isScannerCommandLine (const juce::StringArray& args)
{
    return args.size() >= 3 && args.contains (scanArgument);
}

int PluginCatalog::runScanner (const juce::StringArray& args)
{
   #if JUCE_WINDOWS
    // A plugin that crashes here must not leave a "stopped working" dialog
    // up: the scan would wait for it until it times out.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif

    const auto at = args.indexOf (scanArgument);

    if (at < 0 || at + 2 >= args.size())
        return 2;

    const auto file = args[at + 1];
    const auto resultFile = juce::File (args[at + 2].unquoted());

    juce::AudioPluginFormatManager formats;
    addFormats (formats);

    juce::XmlElement root ("ENCORE_SCAN");
    root.setAttribute ("file", file);

    for (auto* format : formats.getFormats())
    {
        if (! format->fileMightContainThisPluginType (file))
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, file);

        for (auto* d : found)
        {
            // Listing isn't enough (VST3 lists from a file without loading
            // anything): load each plugin and play it briefly, the way a show
            // will. If it crashes or hangs, it does so here, not on stage.
            juce::String error;
            auto instance = formats.createPluginInstance (*d, testSampleRate, testBlockSize, error);

            if (instance == nullptr)
                continue;   // listed but unusable: not offered

            PluginBlock::setUp (*instance, {});
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

            d->numInputChannels = instance->getTotalNumInputChannels();
            d->numOutputChannels = instance->getTotalNumOutputChannels();
            instance = nullptr;

            root.addChildElement (d->createXml().release());
        }
    }

    // Only a complete result file counts as a successful scan.
    return root.writeTo (resultFile) ? 0 : 1;
}

} // namespace encore
