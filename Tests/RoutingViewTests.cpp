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
        }
    }
};

static RoutingViewTests routingViewTests;

} // namespace encore::test
