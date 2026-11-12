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
#include "ui/RoutingView.h"
#include "ui/theme/EncoreTheme.h"

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
    }
};

static RoutingViewTests routingViewTests;

} // namespace encore::test
