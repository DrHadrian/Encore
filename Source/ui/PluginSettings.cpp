// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/PluginSettings.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Paint.h"

namespace encore
{

using namespace theme;

namespace
{
    // Keyboard focus stays with the show (decision 0025).
    void noFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
    }

    void setUpList (juce::ListBox& list, int rowHeight)
    {
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setOutlineThickness (0);
        noFocus (list);
    }

    juce::String fileName (const juce::String& path)
    {
        // Either separator: a catalog may come from another system.
        return path.fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
    }
}

//==============================================================================
namespace
{
    struct Columns { juce::Rectangle<float> name, maker, version; };

    /** NAME | MAKER | VERSION across a row (or the head). */
    Columns columnsIn (juce::Rectangle<int> row)
    {
        auto r = row.reduced (size::mappingPadX, 0).toFloat();
        Columns c;
        c.version = r.removeFromRight ((float) size::pluginsVersionW);
        r.removeFromRight ((float) size::pluginsColumnGap);
        c.maker = r.removeFromRight ((float) size::pluginsMakerW);
        r.removeFromRight ((float) size::pluginsColumnGap);
        c.name = r;
        return c;
    }
}

/** NAME | MAKER | VERSION, alternate rows like the MIDI mappings. */
class PluginSettings::PluginRows final : public juce::ListBoxModel
{
public:
    explicit PluginRows (PluginSettings& o) : owner (o) {}

    int getNumRows() override { return owner.plugins.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, owner.plugins.size()))
            return;

        if (selected)
            g.fillAll (paint::c (colour::selectedRow));
        else if (row % 2 == 1)
            g.fillAll (paint::c (colour::mappingRowAlt));

        g.setColour (paint::c (colour::groupRowLine));
        g.fillRect (0, height - 1, width, 1);

        const auto& d = owner.plugins.getReference (row);
        const auto c = columnsIn ({ 0, 0, width, height });

        paint::text (g, d.name, c.name, type::body, colour::text);
        paint::text (g, d.manufacturerName, c.maker, type::body, selected ? colour::text : colour::textSecondary);
        paint::text (g, d.version, c.version, type::body, selected ? colour::text : colour::textMuted);
    }

private:
    PluginSettings& owner;
};

/** The folders searched, one per row; click to select one to remove. */
class PluginSettings::FolderRows final : public juce::ListBoxModel
{
public:
    explicit FolderRows (PluginSettings& o) : owner (o) {}

    int getNumRows() override { return owner.folders.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, owner.folders.size()))
            return;

        if (selected)
            g.fillAll (paint::c (colour::selectedRow));

        g.setColour (paint::c (colour::groupRowLine));
        g.fillRect (0, height - 1, width, 1);

        const auto r = juce::Rectangle<int> (0, 0, width, height).reduced (size::mappingPadX, 0).toFloat();
        paint::text (g, owner.folders[row], r, type::body, colour::text);
    }

    void selectedRowsChanged (int) override { owner.removeFolderButton.setEnabled (owner.folderList.getSelectedRow() >= 0); }

    juce::String getTooltipForRow (int row) override { return owner.folders[row]; }

private:
    PluginSettings& owner;
};

/** One blocked file: its name, why, and "Try again". */
class PluginSettings::BlockedRow final : public juce::Component,
                                         public juce::SettableTooltipClient
{
public:
    explicit BlockedRow (PluginSettings& o) : owner (o)
    {
        setVariant (tryAgain, Variant::small);
        tryAgain.setTooltip ("Scan this file again (the next Scan loads it in a separate process)");
        tryAgain.onClick = [this]
        {
            // Unblocking refreshes the list (and this row): after the click.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginSettings> (&owner), file = file]
            {
                if (safe != nullptr)
                {
                    safe->catalog.unblock (file);
                    safe->catalog.startScan();
                }
            });
        };
        noFocus (tryAgain);
        addAndMakeVisible (tryAgain);
    }

    void show (const PluginCatalog::Blocked& b)
    {
        file = b.file;
        name = fileName (b.file);
        reason = b.reason;
        setTooltip (b.file);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto textW = (float) (tryAgain.getX() - size::pluginsTryAgainGap - size::mappingPadX);
        const auto x = (float) size::mappingPadX;
        const auto lineH = (float) size::statusTextH;
        paint::text (g, name, { x, (float) size::pluginsBlockedNameTop, textW, lineH }, type::body, colour::text);
        paint::text (g, reason, { x, (float) size::pluginsBlockedReasonTop, textW, lineH }, type::caption, colour::amber);

        g.setColour (paint::c (colour::groupRowLine));
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override
    {
        const auto buttonH = size::buttonSmallHeight;
        const auto w = tryAgain.getBestWidthForHeight (buttonH);
        tryAgain.setBounds (getWidth() - size::mappingPadX - w, (getHeight() - buttonH) / 2, w, buttonH);
    }

private:
    PluginSettings& owner;
    juce::TextButton tryAgain { "Try again" };
    juce::String file, name, reason;
};

class PluginSettings::BlockedRows final : public juce::ListBoxModel
{
public:
    explicit BlockedRows (PluginSettings& o) : owner (o) {}

    int getNumRows() override { return (int) owner.blocked.size(); }

    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        if (! juce::isPositiveAndBelow (row, (int) owner.blocked.size()))
        {
            delete existing;
            return nullptr;
        }

        auto* r = dynamic_cast<BlockedRow*> (existing);

        if (r == nullptr)
        {
            delete existing;
            r = new BlockedRow (owner);
        }

        r->show (owner.blocked[(size_t) row]);
        return r;
    }

private:
    PluginSettings& owner;
};

void PluginSettings::Fill::paint (juce::Graphics& g)
{
    g.fillAll (paint::c (colour::mappingHeader));

    if (lineOnTop)
    {
        g.setColour (paint::c (colour::paneHeaderLine));
        paint::hline (g, 0.0f, 0.0f, (float) getWidth());
    }
}

//==============================================================================
PluginSettings::PluginSettings (PluginCatalog& c)
    : catalog (c),
      pluginRows (std::make_unique<PluginRows> (*this)),
      folderRows (std::make_unique<FolderRows> (*this)),
      blockedRows (std::make_unique<BlockedRows> (*this))
{
    for (auto* g : { &pluginsGroup, &foldersGroup, &blockedGroup })
        addAndMakeVisible (g);

    for (auto* f : { &headerFill, &barFill, &folderBarFill })
        addAndMakeVisible (f);

    pluginList.setModel (pluginRows.get());
    folderList.setModel (folderRows.get());
    blockedList.setModel (blockedRows.get());
    setUpList (pluginList, size::pluginsRowH);
    setUpList (folderList, size::pluginsRowH);
    setUpList (blockedList, size::pluginsBlockedRowH);

    for (auto* l : { &pluginList, &folderList, &blockedList })
        addAndMakeVisible (l);

    addChildComponent (progress);
    addChildComponent (blockedCount);

    scanButton.setTooltip ("Look for new, changed and removed plugins (each new one is tried in a separate process)");
    scanButton.onClick = [this]
    {
        if (catalog.isScanning())
            catalog.cancelScan();
        else
            catalog.startScan();
    };

    rescanButton.setTooltip ("Scan every plugin again, including the blocked ones");
    rescanButton.onClick = [this] { catalog.startFullScan(); };

    addFolderButton.onClick = [this] { addFolder(); };
    removeFolderButton.onClick = [this] { removeFolder(); };
    defaultFoldersButton.setTooltip ("Search the standard VST3 folder only");
    defaultFoldersButton.onClick = [this] { catalog.setFolders (PluginCatalog::getDefaultFolders()); };

    for (auto* b : { &scanButton, &rescanButton, &addFolderButton, &removeFolderButton, &defaultFoldersButton })
    {
        noFocus (*b);
        addAndMakeVisible (b);
    }

    catalog.addChangeListener (this);
    refresh();
    setSize (size::settingsW, getIdealHeight());
}

PluginSettings::~PluginSettings()
{
    catalog.removeChangeListener (this);
    pluginList.setModel (nullptr);
    folderList.setModel (nullptr);
    blockedList.setModel (nullptr);
}

int PluginSettings::getIdealHeight() const
{
    // Like the MIDI tab: the lists fill the dialog.
    return size::settingsH - size::dialogTitleBar - size::tabStrip - size::dialogFooter - 2;
}

void PluginSettings::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PluginSettings::refresh()
{
    const auto newPlugins = catalog.getPlugins();
    const auto newFolders = juce::StringArray::fromTokens (catalog.getFolders().toString(), ";", {});
    const auto newBlocked = catalog.getBlocked();
    const auto newStatus = catalog.getScanStatus();

    const auto samePlugins = newPlugins.size() == plugins.size()
                             && std::equal (newPlugins.begin(), newPlugins.end(), plugins.begin(),
                                            [] (const auto& a, const auto& b) { return a.createIdentifierString() == b.createIdentifierString(); });
    const auto sameBlocked = newBlocked.size() == blocked.size()
                             && std::equal (newBlocked.begin(), newBlocked.end(), blocked.begin(),
                                            [] (const auto& a, const auto& b) { return a.file == b.file && a.reason == b.reason; });

    if (! samePlugins)
    {
        plugins = newPlugins;
        pluginList.updateContent();
        pluginList.repaint();
    }

    if (newFolders != folders)
    {
        folders = newFolders;
        folderList.updateContent();
        folderList.deselectAllRows();
        folderList.repaint();
    }

    if (! sameBlocked)
    {
        blocked = newBlocked;
        blockedList.updateContent();
        blockedList.repaint();
    }

    const auto scanning = catalog.isScanning();
    scanButton.setButtonText (scanning ? "Stop" : "Scan");
    rescanButton.setEnabled (! scanning);
    removeFolderButton.setEnabled (folderList.getSelectedRow() >= 0);

    const auto scan = catalog.getScanProgress();
    const auto showProgress = scanning && scan.total > 0;
    progress.setVisible (showProgress);
    progress.setProgress (showProgress ? (double) scan.current / (double) scan.total : 0.0);

    if (newStatus != status || showProgress != statusOverProgress)
    {
        status = newStatus;
        statusOverProgress = showProgress;
        repaint (statusArea);
    }

    blockedCount.setText (blocked.empty() ? juce::String() : juce::String ((int) blocked.size()));
    blockedCount.setVisible (! blocked.empty());
    layOutCount();

    if (! samePlugins || ! sameBlocked)
        repaint();
}

void PluginSettings::addFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Add a folder to look for plugins in",
                                                   juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<PluginSettings> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        auto path = safe->catalog.getFolders();
        path.addIfNotAlreadyThere (fc.getResult());
        safe->catalog.setFolders (path);
    });
}

void PluginSettings::removeFolder()
{
    const auto row = folderList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, folders.size()))
        return;

    auto path = catalog.getFolders();
    path.remove (row);
    catalog.setFolders (path);
}

//==============================================================================
void PluginSettings::resized()
{
    const auto pad = size::dialogPadX;
    const auto y = size::midiPadTop;
    const auto buttonH = size::buttonHeight + size::buttonDrop;

    // Buttons centred under the bar's 1 px top line.
    auto layOutBar = [] (juce::Rectangle<int> bar) { return bar.getY() + 1 + (bar.getHeight() - 1 - size::buttonHeight) / 2; };

    // PLUGINS: as wide and tall as the MIDI tab's mappings.
    const auto leftW = 2 * size::midiColumnW + size::settingsColumnGap - 2;
    // Its title line also carries the hint: a little taller than the others.
    const auto lift = size::pluginsTitleRowH - size::groupTitleH;
    pluginsGroup.setBounds (pad, y + lift, leftW, juce::jmax (kit::Group::heightFor (size::mappingHeaderH + 4 * size::pluginsRowH + size::pluginsBarH),
                                                              getHeight() - y - lift - size::groupGap));
    pluginsBox = pluginsGroup.getBoxBounds() + pluginsGroup.getPosition();

    auto table = pluginsBox.reduced (1);
    headerFill.setBounds (table.removeFromTop (size::mappingHeaderH));
    auto bar = table.removeFromBottom (size::pluginsBarH);
    barFill.setBounds (bar);
    pluginList.setBounds (table);

    const auto barY = layOutBar (bar);
    scanButton.setBounds (bar.getX() + size::mappingPadX, barY, juce::jmax (scanButton.getBestWidthForHeight (buttonH),
                                                                             size::dialogButtonW), buttonH);
    rescanButton.setBounds (scanButton.getRight() + size::layoutToolbarGap, barY, rescanButton.getBestWidthForHeight (buttonH), buttonH);
    statusArea = bar.withLeft (rescanButton.getRight() + size::mappingPadX).withTrimmedRight (size::mappingPadX);
    progress.setBounds (statusArea.getX(), bar.getY() + size::pluginsProgressTop, statusArea.getWidth(), size::progressH);

    // FOLDERS and BLOCKED on the right.
    const auto rightX = pluginsGroup.getRight() + size::settingsColumnGap;
    const auto foldersBoxH = size::pluginsFoldersBoxH;   // about four rows above its bar
    foldersGroup.setBounds (rightX, y, size::midiRightW, kit::Group::heightFor (foldersBoxH));
    {
        auto box = (foldersGroup.getBoxBounds() + foldersGroup.getPosition()).reduced (1);
        auto folderBar = box.removeFromBottom (size::pluginsBarH);
        folderBarFill.setBounds (folderBar);
        folderList.setBounds (box);

        const auto fy = layOutBar (folderBar);
        addFolderButton.setBounds (folderBar.getX() + size::mappingPadX, fy, addFolderButton.getBestWidthForHeight (buttonH), buttonH);
        removeFolderButton.setBounds (addFolderButton.getRight() + size::layoutToolbarGap, fy, removeFolderButton.getBestWidthForHeight (buttonH), buttonH);
        defaultFoldersButton.setBounds (removeFolderButton.getRight() + size::layoutToolbarGap, fy,
                                        defaultFoldersButton.getBestWidthForHeight (buttonH), buttonH);
    }

    // Its title line carries the count: as tall as the Plugins one.
    const auto blockedY = foldersGroup.getBottom() + size::groupGap + lift;
    blockedGroup.setBounds (rightX, blockedY, size::midiRightW,
                            juce::jmax (kit::Group::heightFor (2 * size::pluginsBlockedRowH), pluginsGroup.getBottom() - blockedY));
    blockedList.setBounds ((blockedGroup.getBoxBounds() + blockedGroup.getPosition()).reduced (1));
    layOutCount();
}

void PluginSettings::layOutCount()
{
    // The count sits at the right end of the Blocked title line.
    const auto w = blockedCount.getIdealWidth();
    const auto titleCentre = blockedGroup.getY() + size::groupTitleH / 2 - size::pluginsCountLift;
    blockedCount.setBounds (blockedGroup.getRight() - w, titleCentre - size::tagChipH / 2, w, size::tagChipH);
}

void PluginSettings::paintOverChildren (juce::Graphics& g)
{
    // Column heads, the hint, the status line, the help and the empty states.
    {
        const auto head = headerFill.getBounds();
        g.setColour (paint::c (colour::paneHeaderLine));
        g.fillRect (head.withTop (head.getBottom() - 1));

        const auto c = columnsIn (head);
        paint::text (g, nameHead, c.name, type::columnHead, colour::textDisabled);
        paint::text (g, makerHead, c.maker, type::columnHead, colour::textDisabled);
        paint::text (g, versionHead, c.version, type::columnHead, colour::textDisabled);
    }

    const auto titleRow = pluginsGroup.getBounds().withHeight (size::groupTitleH).toFloat()
                              .translated (0.0f, (float) (size::groupTitleH - size::pluginsTitleRowH));
    paint::text (g, hint, titleRow, type::noteText, colour::textMuted, juce::Justification::centredRight);

    if (statusOverProgress)
        paint::text (g, status, statusArea.toFloat().withTop ((float) (barFill.getY() + size::pluginsStatusTop))
                                                     .withHeight ((float) size::statusTextH),
                     type::bodySmall, colour::textSecondary);
    else
        paint::text (g, status, statusArea.toFloat(), type::bodySmall, colour::textSecondary);

    if (plugins.isEmpty())
        paint::text (g, noPlugins, pluginList.getBounds().toFloat(), type::body, colour::textTertiary, juce::Justification::centred);

    // Under the blocked rows (or "Nothing blocked"): what blocking means.
    auto rest = blockedList.getBounds();
    rest.removeFromTop (blocked.empty() ? size::pluginsRowH : (int) blocked.size() * size::pluginsBlockedRowH);

    if (blocked.empty())
        paint::text (g, nothingBlocked, blockedList.getBounds().reduced (size::mappingPadX, 0).toFloat().withHeight ((float) size::pluginsRowH),
                     type::body, colour::textTertiary);

    if (rest.getHeight() > size::pluginsHelpPadTop)
    {
        g.setFont (theme::font (type::noteText));
        g.setColour (paint::c (colour::textMuted));
        g.drawFittedText (blockedHelp, rest.reduced (size::mappingPadX, 0).withTrimmedTop (size::pluginsHelpPadTop),
                          juce::Justification::topLeft, 3, 1.0f);
    }
}

} // namespace encore
