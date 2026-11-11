// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The edit view rendered off-screen with a show like the reference board's
// (docs/design/reference/target/edit.png): snapshots for ui-compare.py, and
// the layout checked against the reference geometry.

#include "BoardFixtures.h"
#include "UiSnapshot.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "model/ShowFolder.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

class EditViewTests final : public juce::UnitTest
{
public:
    EditViewTests() : juce::UnitTest ("Edit view (design v3)", "Encore") {}

    static void pump (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

        while (juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    void runTest() override
    {
        ScopedEncoreLook look;
        const juce::TemporaryFile temp;
        const auto root = temp.getFile();
        root.createDirectory();
        const auto showFolder = makeBoardShow (root);

        {
            AppSettings settings (root.getChildFile ("Settings"));
            settings.setLastShowFolder (showFolder);
            juce::XmlElement setup ("DEVICESETUP");
            setup.setAttribute ("deviceType", "Windows Audio");
            setup.setAttribute ("audioOutputDeviceName", "Headset (2- EarPods)");
            setup.setAttribute ("audioInputDeviceName", "Headset (2- EarPods)");
            setup.setAttribute ("audioDeviceRate", 48000);
            setup.setAttribute ("audioDeviceBufferSize", 480);
            settings.setAudioDeviceState (&setup);

            beginTest ("Edit view: draws at the reference size, 1x and 2x");
            {
                MainComponent main (settings, std::make_unique<BoardDeviceType>());
                main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
                pump (1500);   // patches build, the chain arrives, the status text settles

                const auto image = renderSnapshot (main, "edit");
                expect (hasContent (image));
                const auto hiDpi = main.createComponentSnapshot (main.getLocalBounds(), true, 2.0f);
                expectEquals (hiDpi.getWidth(), 2 * theme::size::referenceWidth);
                renderSnapshot (main, "edit@2x", 2.0f);
            }

            beginTest ("Edit view: the minimum size, a huge window and back never break");
            {
                MainComponent main (settings, std::make_unique<BoardDeviceType>());

                for (auto size : { juce::Point<int> (1440, 900), juce::Point<int> (640, 480), juce::Point<int> (3840, 2160),
                                   juce::Point<int> (1, 1), juce::Point<int> (1180, 720) })
                {
                    main.setSize (size.x, size.y);
                    pump (50);
                    main.createComponentSnapshot (main.getLocalBounds().withSizeKeepingCentre (juce::jmin (size.x, 1440), juce::jmin (size.y, 900)));
                }

                const auto minimum = main.getMinimumEditSize();
                expect (minimum.x >= theme::size::minEditWidth && minimum.y >= theme::size::minEditHeight);
            }
        }

        beginTest ("Full list: typing a name (full-list-typing.png)");
        {
            AppSettings settings (root.getChildFile ("Settings musical"));
            settings.setLastShowFolder (makeMusicalShow (root));
            const auto setup = boardDeviceSetup();
            settings.setAudioDeviceState (&setup);

            MainComponent main (settings, std::make_unique<BoardDeviceType>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (1000);

            // Go to cue 5 (digits + Enter), open the full list (F6), select
            // "Title Song coda" (Down from the top) and start typing (Enter).
            main.keyPressed (juce::KeyPress ('5', juce::ModifierKeys(), '5'));
            main.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            main.keyPressed (juce::KeyPress (juce::KeyPress::F6Key));
            pump (300);

            for (int i = 0; i < 10; ++i)
                main.keyPressed (juce::KeyPress (juce::KeyPress::downKey));

            main.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            pump (300);
            expect (hasContent (renderSnapshot (main, "full-list-typing")));
        }

        beginTest ("Edit view: no audio device at all");
        {
            AppSettings settings (root.getChildFile ("Settings without device"));
            settings.setLastShowFolder (showFolder);

            // A device type with no devices: nothing opens.
            struct NoDevices final : juce::AudioIODeviceType
            {
                NoDevices() : juce::AudioIODeviceType ("None") {}
                void scanForDevices() override {}
                juce::StringArray getDeviceNames (bool) const override { return {}; }
                int getDefaultDeviceIndex (bool) const override { return -1; }
                int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return -1; }
                bool hasSeparateInputsAndOutputs() const override { return false; }
                juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override { return nullptr; }
            };

            MainComponent main (settings, std::make_unique<NoDevices>());
            main.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight);
            pump (300);
            expect (hasContent (renderSnapshot (main, "edit-no-device")));
        }
    }
};

static EditViewTests editViewTests;

} // namespace encore::test
