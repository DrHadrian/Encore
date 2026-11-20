// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Routing in the UI (milestone 11): the bottom row's outputs, the Routing
// view, the chain's IN / OUT / split tiles and their block stages, the Stage
// Outputs widget, the pre-show lines and the MIDI Mute output action, against
// the M11 reference boards (docs/design/reference/REFERENCES.md §4).

#include "BoardFixtures.h"
#include "UiSnapshot.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "model/ShowJson.h"
#include "ui/BottomRow.h"
#include "ui/ChainStrip.h"
#include "ui/JackStage.h"
#include "ui/MidiSettings.h"
#include "ui/SettingsWindow.h"
#include "ui/kit/Fader.h"
#include "ui/RoutingView.h"
#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Paint.h"

namespace encore::test
{

class RoutingViewTests final : public juce::UnitTest
{
public:
    RoutingViewTests() : juce::UnitTest ("Routing in the UI", "Encore") {}

    static void pumpUntil (std::function<bool()> done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    static void pump (int ms) { pumpUntil ([] { return false; }, ms); }

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

    /** This computer as on the M11 boards, its own settings folder, the show. */
    struct Rig
    {
        Rig (const juce::File& folder, const juce::File& show)
            : settings (folder)
        {
            settings.setLastShowFolder (show);
            const auto setup = boardDeviceSetup (focusriteSpec());
            settings.setAudioDeviceState (&setup);
            settings.setRouting (makeBoardRouting());
        }

        AppSettings settings;
    };

    /** The first component below parent with this accessible title. */
    static juce::Component* findTitled (juce::Component& parent, const juce::String& title)
    {
        for (auto* c : parent.getChildren())
        {
            if (c->getTitle() == title)
                return c;

            if (auto* found = findTitled (*c, title))
                return found;
        }

        return nullptr;
    }

    /** A mouse event over the middle of c. */
    static juce::MouseEvent eventAt (juce::Component& c)
    {
        const auto centre = c.getLocalBounds().getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), centre, {}, juce::MouseInputSource::defaultPressure,
                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &c, &c, now, centre, now, 1, false };
    }

    /** A click released over the middle of c (what the input cards act on). */
    static void releaseClick (juce::Component& c) { c.mouseUp (eventAt (c)); }

    static juce::String inspectorValue (const JackStage& stage, const juce::String& key)
    {
        for (const auto& section : stage.getInspectorSections())
            for (const auto& row : section.rows)
                if (row.key == key)
                    return row.value;

        return {};
    }

    /** The bottom row board's outputs: "Main" (all), the fixture's three, or eight. */
    static RoutingConfig bottomRowRouting (int count)
    {
        auto r = makeBoardRouting();

        if (count == 1)
        {
            RoutingOutput main;
            main.name = "Main";   // no channels: all outputs
            r.outputs = { main };
            return r;
        }

        if (count == 3)
            return r;

        const auto output = [] (const char* name, std::vector<int> channels, float dB, bool mute)
        {
            RoutingOutput o;
            o.name = name;
            o.channels = std::move (channels);
            o.levelDb = dB;
            o.mute = mute;
            return o;
        };

        r.outputs = { output ("Front of house", { 1, 2 }, 0.0f, false), output ("In-ear Gtr", { 3, 4 }, -6.0f, false),
                      output ("In-ear Drums", { 5, 6 }, -4.0f, false), output ("Amp DI", { 7 }, 0.0f, true),
                      output ("Monitor wedge", { 8 }, -3.0f, false), output ("Stream", { 9, 10 }, -10.0f, false),
                      output ("Recorder", { 11, 12 }, 0.0f, false), output ("Side fill", { 13, 14 }, -2.0f, false) };
        return r;
    }

    static void goToCue (MainComponent& main, int number)
    {
        const auto digits = juce::String (number);

        for (auto c : digits)
            main.keyPressed (juce::KeyPress ((int) c, juce::ModifierKeys(), c));

        main.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    }

    void runTest() override
    {
        ScopedEncoreLook look;
        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();
        const auto show = makeRoutingShow (root);

        beginTest ("Edit with the routing fixture: the bottom row's input and outputs (m11-edit-bottom-row)");
        {
            Rig rig (root.getChildFile ("Settings edit"), show);
            MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (focusriteSpec()));
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);
            goToCue (main, 2);
            pump (1200);

            auto* outputs = findChild<OutputsPane> (main);
            expect (outputs != nullptr);
            expectEquals (outputs != nullptr ? outputs->getNumStrips() : 0, 3);
            expect (outputs != nullptr && outputs->getStrip (2)->getMuteButton().getToggleState(), "Amp DI muted");
            expect (hasContent (renderSnapshot (main, "m11-edit-bottom-row")));

            beginTest ("Dual amp's split paths: IN, gate, SPLIT, two rows, MERGE (selected), delay, OUT (m11-edit-split-merge)");
            {
                auto* strip = findChild<ChainStrip> (main);
                expect (strip != nullptr && strip->getChain() != nullptr && strip->getChain()->hasSplit());
                expectEquals (strip != nullptr ? strip->getIdealHeight() : 0, theme::size::chainSplitPaneH);

                if (strip != nullptr && strip->getChain() != nullptr)
                    strip->selectBlock (strip->getChain()->getMergeIndex());

                pump (400);
                expect (hasContent (renderSnapshot (main, "m11-edit-split-merge")));

                beginTest ("OUT selected: the patch's sends as faders (m11-edit-out-sends)");
                {
                    if (strip != nullptr)
                        strip->selectBlock (ChainStrip::outputSelected);

                    pump (400);
                    expect (hasContent (renderSnapshot (main, "m11-edit-out-sends")));
                }

                beginTest ("IN selected, its name not on this computer, path B off (m11-edit-in-missing)");
                {
                    if (strip != nullptr && strip->getChain() != nullptr)
                    {
                        strip->getChain()->getBlocks()[(size_t) strip->getChain()->getSplitIndex()]->setEnabled (false);
                        strip->selectBlock (ChainStrip::inputSelected);
                    }

                    pump (400);
                    expect (hasContent (renderSnapshot (main, "m11-edit-in-missing")));

                    if (strip != nullptr && strip->getChain() != nullptr)
                        strip->getChain()->getBlocks()[(size_t) strip->getChain()->getSplitIndex()]->setEnabled (true);
                }
            }

            beginTest ("F7 opens Routing over the grid; In-ear selected (m11-routing-inputs-outputs)");
            {
                main.keyPressed (juce::KeyPress (juce::KeyPress::F7Key));
                expect (main.isShowingRouting());
                auto* view = main.getRoutingView();
                expect (view != nullptr && view->getTab() == RoutingView::Tab::inputsOutputs);

                if (view != nullptr)
                    view->selectOutput (1);

                pump (600);
                expect (hasContent (renderSnapshot (main, "m11-routing-inputs-outputs")));

                beginTest ("Routing › Sends, Dual amp's In-ear send selected (m11-routing-sends)");
                {
                    if (view != nullptr)
                    {
                        view->setTab (RoutingView::Tab::sends);
                        view->selectSend (1, 1);
                    }

                    pump (300);
                    expect (hasContent (renderSnapshot (main, "m11-routing-sends")));
                }

                beginTest ("Routing: + Add input on a name that isn't here adds it to this computer");
                {
                    if (view != nullptr)
                        view->setTab (RoutingView::Tab::inputsOutputs);

                    juce::TextButton* addMic = nullptr;
                    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
                    {
                        for (auto* child : c.getChildren())
                        {
                            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getTooltip().contains ("Mic"))
                                addMic = b;

                            find (*child);
                        }
                    };

                    if (view != nullptr)
                        find (*view);

                    expect (addMic != nullptr, "the Mic row's + Add input");

                    // Each step is logged: if this ever stalls in CI, the log says where.
                    logMessage ("  + Add input: click");

                    if (addMic != nullptr && addMic->onClick != nullptr)
                        addMic->onClick();   // (a posted click can be overtaken by a rebuild in a test's tight loop)

                    logMessage ("  + Add input: applied, waiting for the settings");
                    pumpUntil ([&] { return rig.settings.getRouting().findInput ("Mic") != nullptr; }, 3000);
                    logMessage ("  + Add input: done");
                    expect (rig.settings.getRouting().findInput ("Mic") != nullptr, "Mic saved in this computer's routing");
                    expect (main.getRoutingView() != nullptr && main.isShowingRouting());
                }

                main.keyPressed (juce::KeyPress (juce::KeyPress::F7Key));
                expect (! main.isShowingRouting(), "F7 again closes it");
                logMessage ("  closing the window");
            }
        }

        logMessage ("  window closed");

        beginTest ("The bottom row with 1, 3 and 8 outputs, collapsed and in the mixer view (v32-bottom-row, v32-mixer-view)");
        {
            // The board (BBottomRow): each band is the real Edit window's bottom row
            // (input, outputs, health), under the board's caption for it.
            struct Variant { const char* caption; int outputs; bool mixer; };
            const Variant variants[] {
                { "Default after updating: one output \xe2\x80\x9cMain\xe2\x80\x9d = all outputs (today's behaviour)", 1, false },
                { "Collapsed (the default) \xc2\xb7 3 outputs \xc2\xb7 the tab on top opens the mixer view", 3, false },
                { "8 outputs: strips shrink, names shorten (full name on hover), nothing hides or scrolls", 8, false },
                { "Mixer view \xc2\xb7 3 outputs", 3, true },
                { "Mixer view \xc2\xb7 8 outputs: strips keep their width; a row wider than the pane scrolls with the wheel", 8, true },
            };

            constexpr int captionTop = 16, captionH = 14, captionGap = 6, bandGap = 18;
            const auto sheetH = [&]
            {
                auto h = captionTop;

                for (const auto& v : variants)
                    h += captionH + captionGap + (v.mixer ? theme::size::mixerRow : theme::size::bottomRow) + 2 * theme::size::paneTabRise + bandGap;

                return h;
            }();
            juce::Image sheet (juce::Image::RGB, theme::size::referenceWidth, sheetH, true);
            juce::Graphics sheetG (sheet);
            sheetG.fillAll (theme::paint::c (theme::colour::window));
            auto top = captionTop;
            int index = 0;

            for (const auto& v : variants)
            {
                auto spec = focusriteSpec();
                spec.outputs.clear();

                for (int ch = 1; ch <= 14; ++ch)
                    spec.outputs.add ("Output " + juce::String (ch));

                Rig rig (root.getChildFile ("Settings bottom " + juce::String (index)), show);
                rig.settings.setRouting (bottomRowRouting (v.outputs));
                rig.settings.setMixerViewOpen (v.mixer);

                MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (spec));
                main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
                pump (800);

                expect (main.isMixerOpen() == v.mixer, "the mixer view as this computer left it");
                auto& outputs = main.getOutputsPane();
                expectEquals (outputs.getNumStrips(), v.outputs);

                for (int i = 0; i < outputs.getNumStrips(); ++i)
                {
                    expect (outputs.getStrip (i)->isVisible() != v.mixer);
                    expect (outputs.getMixerStrip (i)->isVisible() == v.mixer);
                }

                if (! v.mixer && v.outputs == 8)
                    expect (outputs.getStrip (0)->isValueOnOwnLine(), "narrow strips put the level under the meters");

                if (! v.mixer && v.outputs == 3)
                    expect (! outputs.getStrip (0)->isValueOnOwnLine(), "wide strips keep the level beside the meters");

                if (v.outputs == 3)
                {
                    // The whole Edit window as BEditV32 (v32-edit, v32-edit-mixer).
                    const juce::String name (v.mixer ? "v32-edit-mixer" : "v32-edit");
                    renderSnapshot (main, name);
                    renderSnapshot (main, name + "@2x", 2.0f);

                    // The tab sits centred on the Outputs pane: x 765-809 collapsed, 687-731 open.
                    const auto tab = main.getMixerTab().getBounds();
                    expectEquals (tab.getCentreX(), v.mixer ? 709 : 787, "the mixer tab is centred on the Outputs pane");
                    expectEquals (tab.getWidth(), theme::size::paneTabW);

                    // The amp and cab scale down evenly to fit a shorter stage (never above 1x).
                    if (auto* panel = main.getBlockPanel())
                    {
                        const auto scale = panel->getGearScale();
                        expect (scale <= 1.0f);

                        if (v.mixer)
                            expect (scale < 1.0f && scale > 0.85f, "the mixer view's shorter stage scales the amp: " + juce::String (scale, 3));
                        else
                            expectEquals (scale, 1.0f, "a tall enough stage: 1x");
                    }
                }

                if (auto* pane = outputs.getParentComponent())
                {
                    // The band from the tab's top to the row's bottom.
                    const auto paneArea = main.getLocalArea (pane, pane->getLocalBounds());
                    const auto row = juce::Rectangle<int> (0, paneArea.getY() - theme::size::paneTabRise, main.getWidth(),
                                                           paneArea.getHeight() + theme::size::paneTabRise);
                    const auto band = main.createComponentSnapshot (row, false, 1.0f);
                    expect (hasContent (band));
                    writeSnapshot (band, (v.mixer ? "v32-mixer-view-" : "v32-bottom-row-") + juce::String (index + 1));

                    const theme::type::Style captionStyle { 11.0f, theme::type::Weight::bold, 0.08f, true };
                    theme::paint::text (sheetG, theme::paint::caps (captionStyle, juce::String::fromUTF8 (v.caption)),
                                        { 16.0f, (float) top, 1408.0f, (float) captionH }, captionStyle, theme::colour::textDim);
                    top += captionH + captionGap;
                    sheetG.drawImageAt (band, 0, top);
                    top += band.getHeight() + theme::size::paneTabRise + bandGap;
                }

                // The mixer view with 8 outputs: wider than the pane, the row scrolls sideways with the wheel (no scroll bar).
                if (v.mixer && v.outputs == 8 && outputs.getViewport().getViewedComponent()->getWidth() > outputs.getViewport().getViewWidth())
                {
                    auto& viewport = outputs.getViewport();
                    expect (! viewport.isHorizontalScrollBarShown(), "no scroll bar");
                    juce::MouseWheelDetails wheel {};
                    wheel.deltaY = -1.0f;
                    viewport.mouseWheelMove (eventAt (viewport), wheel);
                    expect (viewport.getViewPositionX() > 0, "the wheel scrolls the row sideways");
                }

                ++index;
            }

            writeSnapshot (sheet, "v32-bottom-row");
        }

        beginTest ("The bottom row works: the mixer tab, a fader, the peak, M, the health status");
        {
            auto spec = focusriteSpec();
            Rig rig (root.getChildFile ("Settings bottom works"), show);
            rig.settings.setRouting (bottomRowRouting (3));
            MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (spec));
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);

            // The tab opens the mixer view (remembered on this computer); the patch area gives up 92 px.
            auto& outputs = main.getOutputsPane();
            const auto collapsedH = outputs.getParentComponent()->getHeight();
            expect (! main.isMixerOpen());
            expectEquals (main.getMixerTab().getTooltip(), juce::String ("Open the mixer view: vertical meters, peaks and level faders"));
            main.getMixerTab().onClick();
            expect (main.isMixerOpen() && rig.settings.getMixerViewOpen());
            expectEquals (outputs.getParentComponent()->getHeight() - collapsedH, theme::size::mixerRow - theme::size::bottomRow);
            expectEquals (main.getMixerTab().getTooltip(), juce::String ("Close the mixer view"));
            expect (main.getInputSummary().getWidth() < theme::size::sidebarWidth, "the input pane narrows to its strip");

            // A fader drives the output's level (double-click: 0 dB), saved with the routing.
            const auto inEar = [&]
            {
                const auto& outs = rig.settings.getRouting().outputs;

                for (int i = 0; i < (int) outs.size(); ++i)
                    if (std::abs (outs[(size_t) i].levelDb) > 0.5f)
                        return i;

                return -1;
            }();
            expect (inEar >= 0, "an output below 0 dB in the fixture");

            if (auto* strip = outputs.getMixerStrip (inEar))
            {
                strip->getFader().mouseDoubleClick (eventAt (strip->getFader()));
                expectWithinAbsoluteError (rig.settings.getRouting().outputs[(size_t) inEar].levelDb, 0.0f, 0.001f);
                expectEquals (strip->getValueCell().getText(), juce::String ("0.0"));
                expectEquals (strip->getFader().getTooltip(), "Level of " + strip->getName() + ". Double-click for 0 dB.");

                // The peak since reset: hot from -1 dBFS; a click resets it.
                strip->setPeaks (1.0f, 0.5f);
                expectEquals (strip->getPeakCell().getText(), juce::String ("0.0"));
                expect (strip->getPeakCell().getColour() == theme::colour::peakHot);
                strip->getPeakCell().onClick();
                expectEquals (strip->getPeakCell().getText(), juce::String::fromUTF8 ("\xe2\x80\x94"));

                // LIM lights while the limiter works.
                strip->setLimiterReduction (2.0f);
                expect (strip->isLimiting());
                expect (strip->getPeakCell().getTooltip() == "Highest level since reset. Click to reset.");

                // M mutes it, here and in the engine; the health status counts it.
                strip->getMuteButton().setToggleState (true, juce::dontSendNotification);
                strip->getMuteButton().onClick();
                pump (200);
                expect (main.getHealthStatus().getNumProblems() >= 1, "a muted output is a thing to check");
                expect (main.getHealthStatus().getTooltip().contains ("muted"), "what to check comes first in the tooltip");
                expect (main.getHealthStatus().getTooltip().endsWith ("Click for system health details (CPU, dropouts, device, latency)."));
            }

            // The health status opens today's health lines (Edit only), with Reset counters.
            main.getHealthStatus().onClick();
            expect (main.isShowingHealthDetails());

            if (auto* details = main.getHealthDetails())
            {
                expectEquals (details->getFrame().getDialogTitle(), juce::String ("System health"));
                auto found = false;

                for (auto* child : details->getFrame().getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == "Reset counters")
                        found = true;

                expect (found, "Reset counters in the footer");
            }

            main.closeHealthDetails();
            expect (! main.isShowingHealthDetails());

            // The tab again closes it.
            main.getMixerTab().onClick();
            expect (! main.isMixerOpen() && ! rig.settings.getMixerViewOpen());

            // The input picker shows the playing patch's input.
            expect (main.getInputSummary().getPicker().getName().isNotEmpty());
            expectEquals (main.getInputSummary().getPicker().getTooltip(),
                          juce::String ("Input of the playing patch. Pick another to change it for that patch."));
        }

        beginTest (juce::String (juce::CharPointer_UTF8 ("Pre-show check with the routing lines and their Routing\xe2\x80\xa6 buttons (m11-preshow-check)")));
        {
            // The board's case: outputs "Main" (all), In-ear on 1/2, Amp DI on 5 (muted; the
            // interface has 4); the patches send to Main where they sent to Front of house,
            // Dual amp and Lead to "Monitor wedge" (not here); "Ambient" reaches nothing.
            const auto folder = makeRoutingShow (root.getChildFile ("preshow"));
            {
                const auto showFile = folder.getChildFile ("show.json");
                juce::String error;
                auto loaded = json::fromJson (showFile.loadFileAsString(), error);
                expect (loaded.has_value(), error);

                if (loaded)
                {
                    const auto toMain = [] (PatchModel& p)
                    {
                        for (auto& send : p.sends)
                            if (send.output == "Front of house")
                                send.output = "Main";
                    };

                    for (auto& p : loaded->patches)
                        toMain (p);

                    std::function<void (RunItem&)> walk = [&] (RunItem& item)
                    {
                        if (item.uniquePatch)
                            toMain (*item.uniquePatch);

                        item.midiOut.clear();   // (no cue sends MIDI on the board)

                        for (auto& child : item.children)
                            walk (child);
                    };
                    walk (loaded->runningOrder);

                    auto ambient = duplicatePatch (loaded->patches.front(), "Ambient");
                    ambient.sends.clear();
                    ambient.setSend ("Main", routing::silentDb, false);
                    ambient.setSend ("In-ear", routing::silentDb, false);
                    ambient.setSend ("Amp DI", 0.0f, true);   // muted
                    ambient.setSend ("Monitor wedge", routing::silentDb, false);
                    loaded->patches.push_back (ambient);
                    expect (showFile.replaceWithText (json::toJson (*loaded)));
                }
            }

            Rig rig (root.getChildFile ("Settings preshow"), folder);
            auto routing = makeBoardRouting();
            RoutingOutput all;
            all.name = "Main";   // no channels: all outputs
            routing.outputs[0] = all;
            routing.outputs[1].channels = { 1, 2 };
            rig.settings.setRouting (routing);

            MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (focusriteSpec()));
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);

            // Live, as before a show (one click on MUTED).
            std::function<juce::TextButton* (juce::Component&, const juce::String&)> findButton = [&] (juce::Component& c, const juce::String& text) -> juce::TextButton*
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == text)
                        return b;

                    if (auto* found = findButton (*child, text))
                        return found;
                }

                return nullptr;
            };

            if (auto* muted = findButton (main, "MUTED"); muted != nullptr && muted->onClick != nullptr)
            {
                muted->setToggleState (false, juce::dontSendNotification);
                muted->onClick();
            }

            main.openPreShowCheck();
            expect (main.isShowingPreShowCheck());
            pump (1200);   // it checks again every second
            expect (hasContent (renderSnapshot (main, "m11-preshow-check")));

            // Routing... closes the check and opens Routing.
            juce::TextButton* fix = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && fix == nullptr && b->getButtonText() == juce::String (juce::CharPointer_UTF8 ("Routing\xe2\x80\xa6")))
                        fix = b;

                    find (*child);
                }
            };
            find (main);
            expect (fix != nullptr, juce::String (juce::CharPointer_UTF8 ("a Routing\xe2\x80\xa6 button")));

            if (fix != nullptr && fix->onClick != nullptr)
                fix->onClick();

            pumpUntil ([&] { return main.isShowingRouting(); }, 2000);
            expect (main.isShowingRouting() && ! main.isShowingPreShowCheck(), juce::String (juce::CharPointer_UTF8 ("Routing\xe2\x80\xa6 opens Routing")));
        }

        beginTest ("MIDI: the action menu at Mute output, a binding to an output not here in amber (m11-settings-midi-mute-output)");
        {
            Rig rig (root.getChildFile ("Settings midi"), show);
            using K = MidiTrigger::Kind;
            const auto bind = [] (MidiAction a, int index, K kind, int channel, int number, const char* output = "")
            {
                MidiBinding b;
                b.action = a;
                b.index = index;
                b.trigger = { kind, channel, number };
                b.output = output;
                return b;
            };
            rig.settings.setMidiBindings ({ bind (MidiAction::nextCue, 1, K::controlChange, 0, 64),
                                            bind (MidiAction::previousCue, 1, K::controlChange, 1, 65),
                                            bind (MidiAction::programRecall, 1, K::programChange, 0, 0),
                                            bind (MidiAction::stomp, 1, K::controlChange, 1, 80),
                                            bind (MidiAction::stomp, 2, K::controlChange, 1, 81),
                                            bind (MidiAction::expression, 1, K::controlChange, 0, 11),
                                            bind (MidiAction::mute, 1, K::note, 10, 36),
                                            bind (MidiAction::muteOutput, 1, K::controlChange, 1, 85, "In-ear"),
                                            bind (MidiAction::muteOutput, 1, K::controlChange, 1, 86, "Monitor wedge"),
                                            bind (MidiAction::tuner, 1, K::controlChange, 1, 69),
                                            bind (MidiAction::panic, 1, K::controlChange, 1, 70) });

            MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (focusriteSpec()));
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);
            main.openSettings (SettingsWindow::Tab::midi);
            pump (200);

            juce::TextButton* first = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && first == nullptr && b->getButtonText() == "Next cue")
                        first = b;

                    find (*child);
                }
            };
            find (main);
            expect (first != nullptr, "the first mapping's action button");

            // The menu as the row's button opens it, Mute output ▸ In-ear highlighted.
            auto image = main.createComponentSnapshot (main.getLocalBounds(), false, 1.0f);
            {
                juce::Graphics g (image);
                auto& lnf = main.getLookAndFeel();
                const auto menu = MidiSettings::makeActionMenu ({ "Front of house", "In-ear", "Amp DI" });
                const auto at = first != nullptr ? main.getLocalArea (first->getParentComponent(), first->getBounds()).getTopLeft() : juce::Point<int>();
                int muteOutputIndex = -1, i = 0;
                const juce::PopupMenu* outputsMenu = nullptr;

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next(); ++i)
                    if (it.getItem().text == "Mute output")
                    {
                        muteOutputIndex = i;
                        outputsMenu = it.getItem().subMenu.get();
                    }

                expect (outputsMenu != nullptr, "Mute output has a submenu");
                const auto items = paintMenu (g, lnf, menu, at, muteOutputIndex);

                if (outputsMenu != nullptr && juce::isPositiveAndBelow (muteOutputIndex, (int) items.size()))
                {
                    const auto overlap = theme::size::menuPadding * 3;
                    const auto parent = items[(size_t) muteOutputIndex];
                    paintMenu (g, lnf, *outputsMenu, { parent.getRight() + theme::size::menuPadding - overlap, parent.getY() - theme::size::menuPadding }, 1);
                }
            }

            writeSnapshot (image, "m11-settings-midi-mute-output");
            expect (hasContent (image));

            // The rows say what they do; the one whose output isn't here says so.
            juce::StringArray actions;
            std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText().startsWith ("Mute output"))
                        actions.add (b->getButtonText());

                    collect (*child);
                }
            };
            collect (main);
            expect (actions.contains ("Mute output: In-ear") && actions.contains ("Mute output: Monitor wedge"), actions.joinIntoString (", "));
            main.closeSettings();
        }

        // The IN and OUT stages act on the patch (a show of its own: these change it).
        const auto jackFolder = root.getChildFile ("jack");
        jackFolder.createDirectory();
        const auto jackShow = makeRoutingShow (jackFolder);

        beginTest ("IN stage: a click on an input's card is the patch's input, live; the saved name not here can't be picked");
        {
            Rig rig (root.getChildFile ("Settings jack"), jackShow);
            MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (focusriteSpec()));
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (800);
            goToCue (main, 2);
            pump (1200);

            auto* strip = findChild<ChainStrip> (main);
            expect (strip != nullptr);

            if (strip != nullptr)
                strip->selectBlock (ChainStrip::inputSelected);

            pump (300);
            auto* stage = findChild<JackStage> (main);
            expect (stage != nullptr && stage->getKind() == JackStage::Kind::input);

            if (stage != nullptr)
            {
                expectEquals (inspectorValue (*stage, "Playing"), juce::String ("Guitar"), "Mic isn't here: Guitar plays instead");

                if (auto* mic = findTitled (*stage, "Input Mic"))
                    releaseClick (*mic);

                pump (200);
                expectEquals (inspectorValue (*stage, "Playing"), juce::String ("Guitar"), "a name that isn't here isn't picked");

                auto* acoustic = findTitled (*stage, "Input Acoustic");
                expect (acoustic != nullptr);

                if (acoustic != nullptr)
                    releaseClick (*acoustic);

                pump (300);
                stage = findChild<JackStage> (main);
                expect (stage != nullptr, "the stage stays (no rebuild)");

                if (stage != nullptr)
                {
                    expectEquals (inspectorValue (*stage, "Playing"), juce::String ("Acoustic"));
                    expect (inspectorValue (*stage, "Saved").isEmpty(), "Acoustic is here: no saved-but-missing line");
                    expect (findTitled (*stage, "Input Mic") == nullptr, "Mic's card goes once the patch doesn't want it");
                }
            }

            beginTest (juce::String (juce::CharPointer_UTF8 ("OUT stage: On turns a send off and on; the fader sets its level; Routing\xe2\x80\xa6 opens Routing")));
            {
                if (strip != nullptr)
                    strip->selectBlock (ChainStrip::outputSelected);

                pump (300);
                stage = findChild<JackStage> (main);
                expect (stage != nullptr && stage->getKind() == JackStage::Kind::output);
                auto* inEar = stage != nullptr ? findTitled (*stage, "Send to In-ear") : nullptr;
                expect (inEar != nullptr);

                if (inEar != nullptr)
                {
                    auto* on = findChild<juce::TextButton> (*inEar);
                    auto* fader = findChild<kit::Fader> (*inEar);
                    expect (on != nullptr && fader != nullptr);

                    if (on != nullptr && fader != nullptr)
                    {
                        expectEquals (on->getTitle(), juce::String ("On"));
                        expectWithinAbsoluteError (fader->getDb(), -6.0f, 0.01f);

                        on->onClick();
                        pump (200);
                        inEar = findTitled (*stage, "Send to In-ear");
                        on = inEar != nullptr ? findChild<juce::TextButton> (*inEar) : nullptr;
                        expect (on != nullptr && on->getTitle() == "Off", "In-ear's send off");

                        if (on != nullptr)
                            on->onClick();

                        pump (200);
                        inEar = findTitled (*stage, "Send to In-ear");
                        on = inEar != nullptr ? findChild<juce::TextButton> (*inEar) : nullptr;
                        fader = inEar != nullptr ? findChild<kit::Fader> (*inEar) : nullptr;
                        expect (on != nullptr && on->getTitle() == "On", "and on again, at its level");
                        expect (fader != nullptr && std::abs (fader->getDb() + 6.0f) < 0.01f);

                        if (fader != nullptr && fader->onChange != nullptr)
                            fader->onChange (-12.0f);

                        pump (200);
                        inEar = findTitled (*stage, "Send to In-ear");
                        fader = inEar != nullptr ? findChild<kit::Fader> (*inEar) : nullptr;
                        expect (fader != nullptr && std::abs (fader->getDb() + 12.0f) < 0.01f, "In-ear at -12 dB");
                    }
                }

                auto* routingButton = stage != nullptr ? findTitled (*stage, juce::String (juce::CharPointer_UTF8 ("Routing\xe2\x80\xa6"))) : nullptr;

                if (routingButton == nullptr && stage != nullptr)
                    for (auto* c : stage->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == juce::String (juce::CharPointer_UTF8 ("Routing\xe2\x80\xa6")))
                            routingButton = b;

                expect (routingButton != nullptr);

                if (auto* b = dynamic_cast<juce::TextButton*> (routingButton); b != nullptr && b->onClick != nullptr)
                    b->onClick();

                pumpUntil ([&] { return main.isShowingRouting(); }, 2000);
                expect (main.isShowingRouting(), juce::String (juce::CharPointer_UTF8 ("Routing\xe2\x80\xa6 opens Routing")));
            }
        }
    }
};

static RoutingViewTests routingViewTests;

} // namespace encore::test
