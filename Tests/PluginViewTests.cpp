// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Plugins in the UI (milestone 10): Settings > Plugins, a plugin block in the
// edit view (block stage and inspector), its window, and the crash notice.

#include "BoardFixtures.h"
#include "PluginFixtures.h"
#include "UiSnapshot.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdlib>

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "engine/blocks/PluginBlock.h"
#include "model/ShowJson.h"
#include "session/PluginCatalog.h"
#include "session/RecoveryFile.h"
#include "ui/ChainStrip.h"
#include "ui/Inspector.h"
#include "ui/PluginSettings.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class PluginViewTests final : public juce::UnitTest
{
public:
    PluginViewTests() : juce::UnitTest ("Plugins in the UI", "Encore") {}

    static void pumpUntil (std::function<bool()> done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    static void pump (int ms) { pumpUntil ([] { return false; }, ms); }

    static void setEnvironment (const char* name, const char* value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value);
       #else
        if (*value == 0)
            unsetenv (name);
        else
            setenv (name, value, 1);
       #endif
    }

    template <typename Type>
    static Type* findChild (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* found = dynamic_cast<Type*> (c))
                return found;

            if (auto* found = findChild<Type> (*c))
                return found;
        }

        return nullptr;
    }

    /** The settings folder's plugin list: the test plugin found, one file blocked. */
    void prepareCatalog (const juce::File& settingsFolder)
    {
        const auto listFile = settingsFolder.getChildFile ("plugins.xml");

        {
            PluginCatalog catalog (listFile, juce::File::getSpecialLocation (juce::File::currentExecutableFile));
            auto folders = juce::FileSearchPath (juce::File (ENCORE_TEST_PLUGIN).getParentDirectory().getFullPathName());
            catalog.setFolders (folders);
            catalog.startScan();
            pumpUntil ([&] { return ! catalog.isScanning(); }, 60000);
            pump (50);
            expectEquals (catalog.getPlugins().size(), 1);
        }

        // As if a scan had blocked a plugin that crashed (and a second folder).
        auto xml = juce::parseXML (listFile);
        expect (xml != nullptr);

        if (xml == nullptr)
            return;

        auto* folders = xml->getChildByName ("FOLDERS");
        folders->setAttribute ("paths", folders->getStringAttribute ("paths") + ";C:\\Program Files\\Common Files\\VST3");

        auto* blocked = xml->getChildByName ("BLOCKED");
        auto* file = blocked->createNewChildElement ("FILE");
        file->setAttribute ("path", "C:\\Program Files\\Common Files\\VST3\\Amp Sim Pro.vst3");
        file->setAttribute ("reason", "Crashed while being scanned");
        expect (xml->writeTo (listFile));
    }

    /** The board's show with the test plugin after the cab in the first patch. */
    void addPluginToShow (const juce::File& showFolder, const juce::String& blockId)
    {
        const auto showFile = showFolder.getChildFile ("show.json");
        juce::String error;
        auto show = json::fromJson (showFile.loadFileAsString(), error);
        expect (show.has_value(), error);

        if (! show)
            return;

        BlockModel plugin;
        plugin.id = blockId;
        plugin.type = pluginBlockType;
        plugin.plugin = { "VST3", "Encore Test Plugin", "Encore", 0, "0.1.0", {} };
        plugin.stomp = 2;

        auto& blocks = show->patches[0].mainLane().blocks;
        blocks.insert (blocks.begin() + 2, plugin);
        expect (showFile.replaceWithText (json::toJson (*show)));
    }

    /** The board's show as on V3EditPlugin: patch Clean is Noise gate, Amp,
        Cab, ValhallaRoom (stomp 3, EXP 1 on Mix from 0 to 60 %), Delay. */
    juce::File makeEditPluginShow (const juce::File& parent, const juce::String& pluginId)
    {
        const auto folder = makeBoardShow (parent);
        const auto showFile = folder.getChildFile ("show.json");
        juce::String error;
        auto show = json::fromJson (showFile.loadFileAsString(), error);
        expect (show.has_value(), error);

        if (! show)
            return folder;

        auto& blocks = show->patches[0].mainLane().blocks;
        std::vector<BlockModel> chain;

        for (const auto* type : { "gate", "nam", "cab" })
            for (auto& b : blocks)
                if (b.type == type)
                    chain.push_back (b);

        for (auto& b : chain)
        {
            b.enabled = true;
            b.stomp = 0;
            b.expression.clear();
        }

        BlockModel plugin;
        plugin.id = pluginId;
        plugin.type = pluginBlockType;
        plugin.plugin = PluginCatalog::toRef (describe ({ "ValhallaRoom", "Valhalla DSP", "2.0.0" }));
        plugin.stomp = 3;
        plugin.expression["p0"] = { 1, 0.0f, 0.6f };
        chain.push_back (plugin);

        for (auto& b : blocks)
        {
            if (b.type == "delay")
            {
                b.enabled = true;
                chain.push_back (b);
            }
        }

        blocks = chain;
        expect (showFile.replaceWithText (json::toJson (*show)));
        return folder;
    }

    void runTest() override
    {
        beginTest ("Set-up: a show with a plugin block, and a scanned plugin list");

        ScopedEncoreLook look;
        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();
        const auto showFolder = makeBoardShow (root);
        const auto blockId = juce::String ("plugin-block");
        addPluginToShow (showFolder, blockId);

        const auto settingsFolder = root.getChildFile ("Settings");
        prepareCatalog (settingsFolder);

        AppSettings settings (settingsFolder);
        settings.setLastShowFolder (showFolder);
        const auto setup = boardDeviceSetup();
        settings.setAudioDeviceState (&setup);

        beginTest ("Settings > Plugins lists what was found, the folders and what's blocked (settings-plugins.png)");
        {
            MainComponent main (settings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);

            main.openSettings (SettingsWindow::Tab::plugins);
            expect (main.isShowingSettings());
            pump (100);
            expect (hasContent (renderSnapshot (main, "settings-plugins-scanned")));

            for (auto size : { juce::Point<int> (640, 480), juce::Point<int> (3840, 2160), juce::Point<int> (1440, 900) })
            {
                main.setSize (size.x, size.y);
                pump (30);
            }

            main.closeSettings();
        }

        beginTest ("Settings > Plugins as on the board: scanning, a plugin and a folder selected, 3 blocked (settings-plugins.png)");
        {
            const auto boardFolder = root.getChildFile ("Settings board catalog");
            writeBoardCatalog (boardFolder.getChildFile ("plugins.xml"));
            AppSettings boardSettings (boardFolder);
            boardSettings.setLastShowFolder (makeBoardShow (boardFolder));
            boardSettings.setAudioDeviceState (&setup);

            MainComponent main (boardSettings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);

            auto& catalog = main.getPluginCatalog();
            expectEquals (catalog.getPlugins().size(), 12);
            expectEquals ((int) catalog.getBlocked().size(), 3);

            main.openSettings (SettingsWindow::Tab::plugins);
            catalog.showScanProgressForTesting (13, 62, "C:\\Program Files\\Common Files\\VST3\\ValhallaSupermassive.vst3");
            expect (catalog.isScanning());
            expectEquals (catalog.getScanProgress().current, 14);
            pump (100);

            // ValhallaRoom and the second folder selected.
            std::vector<juce::ListBox*> lists;
            std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* list = dynamic_cast<juce::ListBox*> (child))
                        lists.push_back (list);
                    else
                        collect (*child);
                }
            };

            if (auto* view = findChild<PluginSettings> (main))
                collect (*view);

            expectEquals ((int) lists.size(), 3, "plugins, folders, blocked");

            if (lists.size() >= 2)
            {
                const auto plugins = catalog.getPlugins();

                for (int i = 0; i < plugins.size(); ++i)
                    if (plugins[i].name == "ValhallaRoom")
                        lists[0]->selectRow (i);

                lists[1]->selectRow (1);
            }

            pump (100);
            expect (hasContent (renderSnapshot (main, "settings-plugins")));

            catalog.cancelScan();
            pump (100);
            expect (! catalog.isScanning());
            main.closeSettings();
        }

        beginTest ("Edit with a plugin block as on the board, the Add block menu open at Plugin > Valhalla DSP (edit-plugin.png)");
        {
            const ScopedBoardPlugins boardPlugins (19);   // 32 parameters: 8 on the plate, 10 in the inspector
            const auto boardFolder = root.getChildFile ("Edit plugin board");
            writeBoardCatalog (boardFolder.getChildFile ("plugins.xml"), false);
            AppSettings boardSettings (boardFolder);
            boardSettings.setLastShowFolder (makeEditPluginShow (boardFolder, "valhalla-room"));
            boardSettings.setAudioDeviceState (&setup);

            MainComponent main (boardSettings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);

            auto* strip = findChild<ChainStrip> (main);
            expect (strip != nullptr);
            pumpUntil ([&] { return strip->getChain() != nullptr && strip->getChain()->getBlocks().size() == 5; }, 10000);
            strip->selectBlock (3);
            pump (300);

            auto* plugin = strip->getChain() != nullptr ? dynamic_cast<PluginBlock*> (strip->getChain()->getBlocks()[3].get()) : nullptr;
            expect (plugin != nullptr && plugin->isLoaded());
            expectEquals (plugin != nullptr ? (int) plugin->getParameters().size() : 0, 32);

            // Menus are windows of their own: draw Add block > Plugin > Valhalla DSP into the snapshot.
            auto image = main.createComponentSnapshot (main.getLocalBounds());
            {
                juce::Graphics g (image);
                auto& lnf = main.getLookAndFeel();
                const auto menu = strip->blockTypeMenu ((int) strip->getChain()->getBlocks().size());
                const auto addTile = main.getLocalArea (strip, strip->getAddTileBounds());

                auto subMenuAt = [] (const juce::PopupMenu& m, const juce::String& text, int& index) -> const juce::PopupMenu*
                {
                    int i = 0;

                    for (juce::PopupMenu::MenuItemIterator it (m); it.next(); ++i)
                    {
                        if (it.getItem().text == text)
                        {
                            index = i;
                            return it.getItem().subMenu.get();
                        }
                    }

                    return nullptr;
                };

                int pluginIndex = -1, makerIndex = -1;
                const auto* makers = subMenuAt (menu, "Plugin", pluginIndex);
                const auto* valhalla = makers != nullptr ? subMenuAt (*makers, "Valhalla DSP", makerIndex) : nullptr;
                expect (makers != nullptr && valhalla != nullptr, "Plugin > Valhalla DSP");

                const auto overlap = theme::size::menuPadding * 3;
                const auto items = paintMenu (g, lnf, menu, { addTile.getRight() + 1, addTile.getY() - 2 }, pluginIndex);

                if (makers != nullptr && juce::isPositiveAndBelow (pluginIndex, (int) items.size()))
                {
                    const auto parent = items[(size_t) pluginIndex];
                    const auto makerItems = paintMenu (g, lnf, *makers, { parent.getRight() + theme::size::menuPadding - overlap,
                                                                          parent.getY() - theme::size::menuPadding }, makerIndex);

                    if (valhalla != nullptr && juce::isPositiveAndBelow (makerIndex, (int) makerItems.size()))
                    {
                        const auto maker = makerItems[(size_t) makerIndex];
                        paintMenu (g, lnf, *valhalla, { maker.getRight() + theme::size::menuPadding - overlap, maker.getY() - theme::size::menuPadding });
                    }
                }
            }

            writeSnapshot (image, "edit-plugin");
            expect (hasContent (image));
        }

        beginTest ("A plugin block in the edit view: block stage and inspector (edit-plugin-test-plugin.png)");
        {
            MainComponent main (settings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);

            auto* strip = findChild<ChainStrip> (main);
            expect (strip != nullptr);
            pumpUntil ([&] { return strip->getChain() != nullptr && strip->getChain()->getBlocks().size() == 6; }, 10000);

            strip->selectBlock (2);
            pump (300);

            auto* plugin = strip->getChain() != nullptr ? dynamic_cast<PluginBlock*> (strip->getChain()->getBlocks()[2].get()) : nullptr;
            expect (plugin != nullptr && plugin->isLoaded());
            expect (findChild<BlockPanel> (main) != nullptr && &findChild<BlockPanel> (main)->getBlock() == plugin);
            expect (hasContent (renderSnapshot (main, "edit-plugin-test-plugin")));

            beginTest ("Showing a plugin block never makes the plugin build its window (that crashes some plugins)");
            {
                const auto editors = root.getChildFile ("editors.txt");
                setEnvironment ("ENCORE_TEST_PLUGIN_EDITORS", editors.getFullPathName().toRawUTF8());

                for (int i = 0; i < 30; ++i)
                {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                    findChild<Inspector> (main)->refresh();
                    findChild<BlockPanel> (main)->refresh();
                }

                expect (! editors.exists(), "no editor was created: " + editors.loadFileAsString().quoted());

                // Opening its window creates exactly one.
               #if JUCE_WINDOWS
                main.openPluginWindow (*plugin);
                expectEquals (juce::StringArray::fromLines (editors.loadFileAsString().trim()).size(), 1);
               #endif

                setEnvironment ("ENCORE_TEST_PLUGIN_EDITORS", "");
            }

            beginTest ("The inspector's Stomp switch menu sets the stomp, even with a refresh before its change arrives");
            {
                juce::ComboBox* stompBox = nullptr;

                std::function<void (juce::Component&)> find = [&] (juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (auto* box = dynamic_cast<juce::ComboBox*> (child); box != nullptr && box->getTitle() == "Stomp switch")
                            stompBox = box;

                        find (*child);
                    }
                };

                auto* inspector = findChild<Inspector> (main);
                expect (inspector != nullptr);
                find (*inspector);
                expect (stompBox != nullptr);

                if (stompBox != nullptr && inspector != nullptr)
                {
                    expectEquals (stompBox->getSelectedId(), 3, "stomp 2");

                    // As a pick in the popup does: the change is sent asynchronously,
                    // and the 30 Hz refresh can run first.
                    stompBox->setSelectedId (6, juce::sendNotificationAsync);
                    inspector->refresh();
                    pump (100);
                    inspector->refresh();
                    expectEquals (stompBox->getSelectedId(), 6);
                    expectEquals (plugin->getStomp(), 5);
                }
            }

           #if JUCE_WINDOWS
            beginTest ("The plugin's own window opens, and follows its block when the patch is rebuilt");
            {
                main.openPluginWindow (*plugin);
                expect (main.isShowingPluginWindow (blockId));
                pump (100);
            }
           #endif
        }

        beginTest ("After a crash inside a plugin, that block starts switched off with a notice");
        {
            // What the crash handler and recovery file leave behind.
            settingsFolder.getChildFile ("plugin-crash.txt").replaceWithText (blockId + "\tEncore Test Plugin");
            RecoveryFile recovery (settingsFolder.getChildFile ("recovery.json"));
            RecoveryFile::State state;
            state.showFolder = showFolder.getFullPathName();
            state.cue = 0;
            recovery.update (state);

            MainComponent main (settings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);

            auto* strip = findChild<ChainStrip> (main);
            pumpUntil ([&] { return strip->getChain() != nullptr && strip->getChain()->getBlocks().size() == 6; }, 10000);

            auto* plugin = dynamic_cast<PluginBlock*> (strip->getChain()->getBlocks()[2].get());
            expect (plugin != nullptr && plugin->getFault() == PluginBlock::Fault::crashedLastTime);
            expect (! settingsFolder.getChildFile ("plugin-crash.txt").exists(), "used once");

            strip->selectBlock (2);
            pump (300);
            expect (hasContent (renderSnapshot (main, "edit-plugin-crashed")));
        }
    }
};

static PluginViewTests pluginViewTests;

} // namespace encore::test
