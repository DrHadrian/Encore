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
#include "ui/BottomRow.h"
#include "ui/ChainStrip.h"
#include "ui/JackStage.h"
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

                    if (addMic != nullptr && addMic->onClick != nullptr)
                        addMic->onClick();   // (a posted click can be overtaken by a rebuild in a test's tight loop)

                    pumpUntil ([&] { return rig.settings.getRouting().findInput ("Mic") != nullptr; }, 3000);
                    expect (rig.settings.getRouting().findInput ("Mic") != nullptr, "Mic saved in this computer's routing");
                    expect (main.getRoutingView() != nullptr && main.isShowingRouting());
                }

                main.keyPressed (juce::KeyPress (juce::KeyPress::F7Key));
                expect (! main.isShowingRouting(), "F7 again closes it");
            }
        }

        beginTest ("The bottom row with 1, 3 and 8 outputs, compact and full (m11-bottom-row)");
        {
            // The sheet: each band is the real Edit window's bottom row (input,
            // outputs, system health), under the board's caption for it.
            struct Variant { const char* caption; int outputs; bool full; };
            const Variant variants[] {
                { "Default after updating: one output \xe2\x80\x9cMain\xe2\x80\x9d = all outputs (today's behaviour)", 1, false },
                { "3 outputs \xc2\xb7 compact (the default)", 3, false },
                { "8 outputs \xc2\xb7 compact: strips shrink, names shorten, nothing hides", 8, false },
                { "8 outputs \xc2\xb7 full: strips keep 170 px and the row scrolls sideways (the toggle is remembered on this computer)", 8, true },
            };

            constexpr int sheetH = 760, captionTop = 16, captionH = 14, bandTop = 36, bandPitch = 142;
            juce::Image sheet (juce::Image::RGB, theme::size::referenceWidth, sheetH, true);
            juce::Graphics sheetG (sheet);
            sheetG.fillAll (theme::paint::c (theme::colour::window));
            int index = 0;

            for (const auto& v : variants)
            {
                auto spec = focusriteSpec();
                spec.outputs.clear();

                for (int ch = 1; ch <= 14; ++ch)
                    spec.outputs.add ("Output " + juce::String (ch));

                Rig rig (root.getChildFile ("Settings bottom " + juce::String (index)), show);
                rig.settings.setRouting (bottomRowRouting (v.outputs));
                rig.settings.setOutputStripsFull (v.full);

                MainComponent main (rig.settings, std::make_unique<BoardDeviceType> (spec));
                main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
                pump (800);

                auto* outputs = findChild<OutputsPane> (main);
                expect (outputs != nullptr);
                expectEquals (outputs != nullptr ? outputs->getNumStrips() : 0, v.outputs);

                if (outputs != nullptr && outputs->getParentComponent() != nullptr)
                {
                    auto* pane = outputs->getParentComponent();
                    const auto row = main.getLocalArea (pane, pane->getLocalBounds()).withX (0).withWidth (main.getWidth());
                    const auto band = main.createComponentSnapshot (row, false, 1.0f);
                    expect (hasContent (band));
                    writeSnapshot (band, "m11-bottom-row-" + juce::String (index + 1));

                    const auto top = bandTop + index * bandPitch;
                    sheetG.drawImageAt (band, 0, top);
                    const theme::type::Style captionStyle { 11.0f, theme::type::Weight::bold, 0.08f, true };
                    theme::paint::text (sheetG, theme::paint::caps (captionStyle, juce::String::fromUTF8 (v.caption)),
                                        { 16.0f, (float) (top - bandTop + captionTop), 1408.0f, (float) captionH },
                                        captionStyle, theme::colour::textDisabled);
                }

                // Full: wider than the pane, the row scrolls sideways with the wheel (no scroll bar).
                if (v.full && outputs != nullptr)
                {
                    auto* viewport = findChild<juce::Viewport> (*outputs);
                    expect (viewport != nullptr && ! viewport->isHorizontalScrollBarShown(), "no scroll bar");

                    if (viewport != nullptr)
                    {
                        expect (viewport->getViewedComponent()->getWidth() > viewport->getViewWidth(), "the strips are wider than the pane");
                        juce::MouseWheelDetails wheel {};
                        wheel.deltaY = -1.0f;
                        viewport->mouseWheelMove (eventAt (*viewport), wheel);
                        expect (viewport->getViewPositionX() > 0, "the wheel scrolls the row sideways");
                    }
                }

                ++index;
            }

            writeSnapshot (sheet, "m11-bottom-row");
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

            beginTest ("OUT stage: On turns a send off and on; the fader sets its level; Routing... opens Routing");
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

                auto* routingButton = stage != nullptr ? findTitled (*stage, "Routing...") : nullptr;

                if (routingButton == nullptr && stage != nullptr)
                    for (auto* c : stage->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Routing...")
                            routingButton = b;

                expect (routingButton != nullptr);

                if (auto* b = dynamic_cast<juce::TextButton*> (routingButton); b != nullptr && b->onClick != nullptr)
                    b->onClick();

                pumpUntil ([&] { return main.isShowingRouting(); }, 2000);
                expect (main.isShowingRouting(), "Routing... opens Routing");
            }
        }
    }
};

static RoutingViewTests routingViewTests;

} // namespace encore::test
