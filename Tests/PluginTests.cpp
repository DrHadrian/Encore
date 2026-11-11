// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "TestSignals.h"
#include "AllocationCounter.h"
#include "FakeDevice.h"

#include <juce_events/juce_events.h>

#include "engine/blocks/PluginBlock.h"
#include "engine/core/Realtime.h"
#include "model/ShowJson.h"
#include "session/PatchBuilder.h"
#include "session/PluginCatalog.h"
#include "session/PreShowCheck.h"
#include "session/ShowSession.h"

#include <cstdlib>

namespace encore::test
{

/** VST3 hosting (milestone 10), with the test plugin built next to the tests. */
class PluginTests final : public juce::UnitTest
{
public:
    PluginTests() : juce::UnitTest ("Plugins", "Encore") {}

    static void pumpUntil (std::function<bool()> done, int timeoutMs = 60000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    /** The scanner process inherits this process's environment. */
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

    static juce::File testPluginFile() { return juce::File (ENCORE_TEST_PLUGIN); }

    std::shared_ptr<PluginCatalog> makeCatalog (const juce::File& folder)
    {
        auto catalog = std::make_shared<PluginCatalog> (folder.getChildFile ("plugins.xml"),
                                                        juce::File::getSpecialLocation (juce::File::currentExecutableFile));
        catalog->setFolders (juce::FileSearchPath (testPluginFile().getParentDirectory().getFullPathName()));
        return catalog;
    }

    void scan (PluginCatalog& catalog, bool everything)
    {
        if (everything)
            catalog.startFullScan();
        else
            catalog.startScan();

        pumpUntil ([&] { return ! catalog.isScanning(); });
        expect (! catalog.isScanning(), "the scan finished");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // its last update (saves the list)
    }

    static Parameter* findParameterNamed (Block& block, const juce::String& name)
    {
        for (const auto& p : block.getParameters())
            if (p->getSpec().name == name)
                return p.get();

        return nullptr;
    }

    /** Runs blocks of a constant 0.5 through the block; returns the last output sample. */
    static float play (Block& block, int blocks = 1)
    {
        std::vector<float> buffer ((size_t) blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            std::fill (buffer.begin(), buffer.end(), 0.5f);
            float* channels[1] = { buffer.data() };
            block.process (channels, 1, blockSize);
        }

        return buffer.back();
    }

    void runTest() override
    {
        juce::MessageManager::getInstance();

        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("encore-plugins-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        temp.createDirectory();

        beginTest ("The test plugin was built");
        expect (testPluginFile().exists(), testPluginFile().getFullPathName());

        auto catalog = makeCatalog (temp);

        beginTest ("A scan finds plugins, each examined in its own process, and remembers them");
        {
            scan (*catalog, false);
            const auto plugins = catalog->getPlugins();
            expectEquals (plugins.size(), 1);
            expectEquals (plugins[0].name, juce::String ("Encore Test Plugin"));
            expectEquals (plugins[0].manufacturerName, juce::String ("Encore"));
            expect (catalog->getBlocked().empty());
            expect (catalog->getScanStatus().startsWith ("1 plugin"), catalog->getScanStatus());

            // Another start: the list comes from disk, nothing needs scanning.
            auto again = makeCatalog (temp);
            expectEquals (again->getPlugins().size(), 1);
            expect (again->find (PluginCatalog::toRef (plugins[0])).has_value());
        }

        beginTest ("A plugin that crashes while being scanned is blocked; Encore carries on");
        {
            auto crashing = makeCatalog (temp.getChildFile ("crash"));
            setEnvironment ("ENCORE_TEST_PLUGIN_SCAN", "crash");
            scan (*crashing, true);
            setEnvironment ("ENCORE_TEST_PLUGIN_SCAN", "");

            expectEquals (crashing->getPlugins().size(), 0);
            const auto blocked = crashing->getBlocked();
            expectEquals ((int) blocked.size(), 1);

            if (! blocked.empty())
            {
                expectEquals (blocked[0].reason, juce::String ("Crashed while being scanned"));
                expect (crashing->findBlocked ({ "VST3", "Encore Test Plugin", "Encore", 0, {}, blocked[0].file }).has_value());
            }

            // Blocked files aren't tried again by a normal scan...
            scan (*crashing, false);
            expectEquals (crashing->getPlugins().size(), 0);

            // ...until unblocked (fixed plugin, here: no crash any more).
            crashing->unblock (blocked.empty() ? juce::String() : blocked[0].file);
            scan (*crashing, false);
            expectEquals (crashing->getPlugins().size(), 1);
            expect (crashing->getBlocked().empty());
        }

        beginTest ("A plugin that hangs while being scanned is stopped and blocked");
        {
            auto hanging = makeCatalog (temp.getChildFile ("hang"));
            hanging->setScanTimeout (juce::RelativeTime::seconds (3));
            setEnvironment ("ENCORE_TEST_PLUGIN_SCAN", "hang");
            scan (*hanging, true);
            setEnvironment ("ENCORE_TEST_PLUGIN_SCAN", "");

            const auto blocked = hanging->getBlocked();
            expectEquals ((int) blocked.size(), 1);

            if (! blocked.empty())
                expectEquals (blocked[0].reason, juce::String ("Didn't finish loading within 3 s while being scanned"));
        }

        if (catalog->getPlugins().isEmpty())
        {
            expect (false, "no test plugin to host: the rest is skipped");
            temp.deleteRecursively();
            return;
        }

        const auto description = catalog->getPlugins()[0];
        PluginContext context;
        context.catalog = catalog;

        BlockModel model;
        model.id = "p1";
        model.type = pluginBlockType;
        model.plugin = PluginCatalog::toRef (description);

        beginTest ("A plugin block plays the plugin; its parameters are Encore parameters");
        {
            juce::String error;
            auto block = createPluginBlock (model, spec, context, error);
            expect (error.isEmpty(), error);
            block->prepare (spec);

            auto& plugin = dynamic_cast<PluginBlock&> (*block);
            expect (plugin.isLoaded());
            expectEquals (plugin.getModelId(), juce::String ("p1"));
            expectEquals (plugin.getLatencySamples(), 32);
            expect (plugin.hasTail(), "plugins always ring out until silent");

            auto* gain = findParameterNamed (*block, "Gain");
            expect (gain != nullptr && findParameterNamed (*block, "Mode") != nullptr);

            expectWithinAbsoluteError (play (*block, 2), 0.5f, 1.0e-4f);   // unity gain (0.5 normalised)

            gain->setValue (0.25f);   // normalised: gain 0.5
            expectWithinAbsoluteError (play (*block, 2), 0.25f, 1.0e-4f);
            expect (gain->toText (0.25f).startsWith ("0.5"), gain->toText (0.25f));

            beginTest ("Processing a plugin block allocates nothing on the audio thread");
            {
                std::vector<float> buffer ((size_t) blockSize, 0.5f);
                float* channels[1] = { buffer.data() };
                const auto before = realtimeAllocationCount();

                for (int i = 0; i < 200; ++i)
                {
                    gain->setValue (i % 2 == 0 ? 0.3f : 0.6f);
                    const rt::ScopedRealtimeSection realtime;
                    block->process (channels, 1, blockSize);
                }

                expectEquals (realtimeAllocationCount() - before, 0);
            }
        }

        beginTest ("Switching a plugin block off and on (stomp), and resetting it, works while playing");
        {
            juce::String error;
            auto block = createPluginBlock (model, spec, context, error);
            block->prepare (spec);
            findParameterNamed (*block, "Gain")->setValue (0.25f);
            expectWithinAbsoluteError (play (*block, 2), 0.25f, 1.0e-4f);

            block->setEnabled (false);
            expectWithinAbsoluteError (play (*block, 10), 0.5f, 1.0e-4f);   // after the 10 ms crossfade: dry

            block->setEnabled (true);
            expectWithinAbsoluteError (play (*block, 10), 0.25f, 1.0e-4f);

            // A reset (panic) asked for on the audio thread happens on the message thread.
            block->reset();
            block->collectGarbage();
            expectWithinAbsoluteError (play (*block, 2), 0.25f, 1.0e-4f);
        }

        beginTest ("A plugin's settings are saved in the show and come back");
        {
            PatchModel patch;
            patch.name = "Plugin patch";
            patch.mainLane().blocks = { model };

            juce::StringArray problems;
            auto chain = buildChain (patch, {}, spec, problems, context);
            expect (problems.isEmpty(), problems.joinIntoString ("; "));

            auto* gain = findParameterNamed (*chain->getBlocks()[0], "Gain");
            gain->setValue (0.125f);
            play (*chain->getBlocks()[0]);

            captureChain (*chain, patch, {});
            const auto& saved = patch.mainLane().blocks[0];
            expect (saved.state.isNotEmpty());
            expectEquals (saved.plugin.name, juce::String ("Encore Test Plugin"));
            expect (saved.params.empty(), "the plugin's state holds its settings");

            // Through the show file and back.
            ShowModel show = makeDefaultShow ("Plugins");
            expectEquals (show.requiredSchemaVersion(), 1, "no plugins: older Encore can still open it");
            show.patches.push_back (patch);
            expectEquals (show.requiredSchemaVersion(), 2);

            juce::String error;
            const auto json = json::toJson (show);
            expect (json.contains ("\"schemaVersion\": 2"));
            auto reread = json::fromJson (json, error);
            expect (reread.has_value(), error);
            expect (reread->patches.back() == patch);

            auto rebuilt = buildChain (reread->patches.back(), {}, spec, problems, context);
            expectWithinAbsoluteError (findParameterNamed (*rebuilt->getBlocks()[0], "Gain")->getValue(), 0.125f, 1.0e-4f);
            expectWithinAbsoluteError (play (*rebuilt->getBlocks()[0], 2), 0.125f, 1.0e-4f);

            beginTest ("A knob on an expression pedal is saved at its own value, not the pedal's");
            {
                auto& b = reread->patches.back().mainLane().blocks[0];
                const auto gainId = findParameterNamed (*rebuilt->getBlocks()[0], "Gain")->getSpec().id;
                b.expression[gainId] = { 1, 0.0f, 1.0f };
                b.params[gainId] = 0.4f;
                auto pedalChain = buildChain (reread->patches.back(), {}, spec, problems, context);
                expectWithinAbsoluteError (findParameterNamed (*pedalChain->getBlocks()[0], "Gain")->getValue(), 0.4f, 1.0e-4f);

                pedalChain->applyExpression (1, 0.9f);
                PatchModel copy = reread->patches.back();
                captureChain (*pedalChain, copy, {});
                expectWithinAbsoluteError (copy.mainLane().blocks[0].params[gainId], 0.4f, 1.0e-4f);
            }
        }

        beginTest ("A plugin that isn't installed passes the signal through and keeps its settings");
        {
            PatchModel patch;
            patch.name = "Away";
            auto missing = model;
            missing.plugin.name = "Not Here";
            missing.plugin.maker = "Nobody";
            missing.plugin.uid = 12345;
            missing.state = "c29tZSBzdGF0ZQ==";
            missing.expression["7"] = { 2, 0.0f, 1.0f };
            missing.params["7"] = 0.3f;
            patch.mainLane().blocks = { missing };

            juce::StringArray problems;
            auto chain = buildChain (patch, {}, spec, problems, context);
            expectEquals (problems.size(), 1);
            expect (problems[0].contains ("Not Here isn't installed on this computer"), problems[0]);

            auto& block = *chain->getBlocks()[0];
            expectWithinAbsoluteError (play (block), 0.5f, 1.0e-6f);

            const auto before = patch;
            captureChain (*chain, patch, {});
            expect (patch == before, "saving changes nothing");

            juce::StringArray noCatalogProblems;
            buildChain (patch, {}, spec, noCatalogProblems, {});
            expect (noCatalogProblems[0].contains ("can't be loaded here"));
        }

        beginTest ("Crash guard: broken audio switches the plugin off, the dry signal passes");
        {
            juce::String error;
            auto block = createPluginBlock (model, spec, context, error);
            block->prepare (spec);
            auto& plugin = dynamic_cast<PluginBlock&> (*block);
            auto* mode = findParameterNamed (*block, "Mode");
            const auto faultsBefore = PluginBlock::getFaultCount();

            mode->setValue (1.0f / 3.0f);   // NaN
            expectWithinAbsoluteError (play (*block), 0.5f, 1.0e-6f);
            expect (plugin.getFault() == PluginBlock::Fault::badOutput);
            expect (PluginBlock::getFaultCount() > faultsBefore);
            expectEquals (plugin.getFaultText(), juce::String ("Sent broken audio and was switched off"));
            expect (plugin.canTurnBackOn());

            mode->setValue (0.0f);
            plugin.turnBackOn();
            expect (plugin.getFault() == PluginBlock::Fault::none);
            play (*block, 2);
            expect (plugin.getFault() == PluginBlock::Fault::none);
        }

        beginTest ("Crash guard: a plugin slower than the buffer three times running is switched off");
        {
            juce::String error;
            auto block = createPluginBlock (model, spec, context, error);
            block->prepare (spec);
            auto& plugin = dynamic_cast<PluginBlock&> (*block);
            findParameterNamed (*block, "Mode")->setValue (2.0f / 3.0f);

            play (*block, 2);
            expect (plugin.getFault() == PluginBlock::Fault::none, "two slow buffers are tolerated");
            play (*block);
            expect (plugin.getFault() == PluginBlock::Fault::tooSlow);
            expectWithinAbsoluteError (play (*block), 0.5f, 1.0e-6f);
        }

       #if JUCE_WINDOWS
        beginTest ("Crash guard: a plugin that crashes while playing is switched off and never called again");
        {
            juce::String error;
            auto block = createPluginBlock (model, spec, context, error);
            block->prepare (spec);
            auto& plugin = dynamic_cast<PluginBlock&> (*block);
            findParameterNamed (*block, "Mode")->setValue (1.0f);

            expectWithinAbsoluteError (play (*block), 0.5f, 1.0e-6f);
            expect (plugin.getFault() == PluginBlock::Fault::crashed);
            expect (plugin.getInstance() == nullptr);
            expect (! plugin.canTurnBackOn());
            expectWithinAbsoluteError (play (*block, 3), 0.5f, 1.0e-6f);
            expect (plugin.getState().isEmpty(), "never asked for its state again");
        }
       #endif

        beginTest ("The crash handler can tell which plugin was running");
        {
            expect (PluginBlock::getPluginInUseOnThisThread() == nullptr);
            const auto [blockId, pluginName] = PluginBlock::parseCrashLabel ("p1\tEncore Test Plugin\n");
            expectEquals (blockId, juce::String ("p1"));
            expectEquals (pluginName, juce::String ("Encore Test Plugin"));
        }

        beginTest ("A plugin that crashed Encore last time starts switched off");
        {
            PatchModel patch;
            patch.name = "Suspect";
            patch.mainLane().blocks = { model };
            auto suspect = context;
            suspect.crashedLastTime = { "p1" };

            juce::StringArray problems;
            auto chain = buildChain (patch, {}, spec, problems, suspect);
            auto& plugin = dynamic_cast<PluginBlock&> (*chain->getBlocks()[0]);
            expect (plugin.getFault() == PluginBlock::Fault::crashedLastTime);
            expectWithinAbsoluteError (play (plugin), 0.5f, 1.0e-6f);
            plugin.turnBackOn();
            expect (plugin.getFault() == PluginBlock::Fault::none);
        }

        beginTest ("A show with a plugin block builds, saves and reopens through the session");
        {
            FakeDevice device;
            AudioEngine engine;
            engine.audioDeviceAboutToStart (&device);
            const auto showFolder = temp.getChildFile ("Plugins.encore");

            {
                ShowSession session (engine);
                session.setPluginCatalog (catalog);
                expect (session.newShow (showFolder, "Plugins").wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); }, 10000);

                session.addPluginBlock (PluginCatalog::toRef (description), 0);
                pumpUntil ([&] { return ! session.isBuilding(); }, 10000);
                expect (session.takeProblems().isEmpty());
                expect (session.save().wasOk());
            }

            juce::String readError;
            const auto saved = json::fromJson (showFolder.getChildFile ("show.json").loadFileAsString(), readError);
            expect (saved.has_value() && saved->patches[0].mainLane().blocks[0].type == pluginBlockType);
            expect (saved.has_value() && saved->patches[0].mainLane().blocks[0].state.isNotEmpty());
            expect (showFolder.getChildFile ("show.json").loadFileAsString().contains ("\"schemaVersion\": 2"));

            {
                ShowSession session (engine);
                session.setPluginCatalog (catalog);
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); }, 10000);
                expect (session.takeProblems().isEmpty());
                expect (session.getPluginProblems().empty());
            }

            beginTest ("Plugins that don't play are in the pre-show check");
            {
                ShowSession session (engine);   // no plugin catalog: nothing can load
                expect (session.openShow (showFolder).wasOk());
                pumpUntil ([&] { return ! session.isBuilding(); }, 10000);

                const auto problems = session.getPluginProblems();
                expectEquals ((int) problems.size(), 1);

                PreShowFacts facts;
                facts.pluginProblems = problems;
                const auto checks = runPreShowChecks (facts);
                const auto it = std::find_if (checks.begin(), checks.end(), [] (const auto& c) { return c.title == "1 plugin not playing"; });
                expect (it != checks.end());

                if (it != checks.end())
                {
                    expect (it->status == PreShowCheckResult::Status::problem);
                    expect (it->fix == PreShowCheckResult::Fix::pluginSettings);
                    expect (it->detail.contains ("can't be loaded here"), it->detail);
                }
            }

            engine.audioDeviceStopped();
        }

        temp.deleteRecursively();
    }
};

static PluginTests pluginTests;

} // namespace encore::test
