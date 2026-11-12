// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "session/PluginCatalog.h"
#include "ui/kit/Chip.h"
#include "ui/kit/Group.h"
#include "ui/kit/ProgressBar.h"

namespace encore
{

/** Settings > Plugins: the VST3 plugins Encore found, where it looks, and the
    plugins it blocked (with the reason and "Try again"). Scanning runs in the
    background; this view only shows the catalog and starts things. All of
    it belongs to this computer. Message thread only. */
class PluginSettings final : public juce::Component,
                             private juce::ChangeListener
{
public:
    explicit PluginSettings (PluginCatalog&);
    ~PluginSettings() override;

    /** Height the content needs (the window scrolls when smaller). */
    int getIdealHeight() const;

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

private:
    class PluginRows;
    class FolderRows;
    class BlockedRows;
    class BlockedRow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void addFolder();
    void removeFolder();
    void layOutCount();

    PluginCatalog& catalog;

    kit::Group pluginsGroup { "Plugins" }, foldersGroup { "Folders" }, blockedGroup { "Blocked" };

    struct Fill final : juce::Component
    {
        explicit Fill (bool line) : lineOnTop (line) {}
        void paint (juce::Graphics&) override;
        const bool lineOnTop;
    };

    Fill headerFill { false }, barFill { true }, folderBarFill { true };

    juce::Array<juce::PluginDescription> plugins;
    juce::StringArray folders;
    std::vector<PluginCatalog::Blocked> blocked;
    juce::String status;
    bool statusOverProgress = false;
    kit::ProgressBar progress;
    kit::Chip blockedCount { kit::ChipStyle::amber };

    std::unique_ptr<PluginRows> pluginRows;
    std::unique_ptr<FolderRows> folderRows;
    std::unique_ptr<BlockedRows> blockedRows;
    juce::ListBox pluginList, folderList, blockedList;

    juce::TextButton scanButton { "Scan" }, rescanButton { "Scan all again" },
                     addFolderButton { "+ Add folder..." }, removeFolderButton { "Remove" }, defaultFoldersButton { "Defaults" };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> pluginsBox, statusArea;
    const juce::String nameHead { "NAME" }, makerHead { "MAKER" }, versionHead { "VERSION" },
                       noPlugins { "No plugins yet. Scan looks in the folders on the right." },
                       nothingBlocked { "Nothing blocked" },
                       hint { juce::String (juce::CharPointer_UTF8 ("Add one to a patch with the + in the signal chain \xe2\x80\xba Plugin")) },
                       blockedHelp { "Scan skips blocked files. Try again unblocks one, so the next Scan tries it in a separate process." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSettings)
};

} // namespace encore
