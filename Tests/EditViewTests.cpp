// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The edit view rendered off-screen with a show like the reference board's
// (docs/design/reference/target/edit.png): snapshots for ui-compare.py, and
// the layout checked against the reference geometry.

#include "UiSnapshot.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include "app/AppSettings.h"
#include "app/MainComponent.h"
#include "model/ShowFolder.h"
#include "ui/theme/EncoreTheme.h"

namespace encore::test
{

/** An audio interface that plays silence from its own thread: like the
    reference board's "Headset (2- EarPods)" on Windows Audio, 48 kHz,
    480 samples, 10 ms in and out (20.0 ms round trip). */
class BoardDevice final : public juce::AudioIODevice,
                          private juce::Thread
{
public:
    BoardDevice() : juce::AudioIODevice ("Headset (2- EarPods)", "Windows Audio"), juce::Thread ("Board device") {}
    ~BoardDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override       { return { "Output 1", "Output 2" }; }
    juce::StringArray getInputChannelNames() override        { return { "Input channel 1", "Input channel 2" }; }
    juce::Array<double> getAvailableSampleRates() override   { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override      { return { 480 }; }
    int getDefaultBufferSize() override                      { return 480; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double, int) override
    {
        activeIns = ins;
        activeOuts = outs;
        opened = true;
        return {};
    }

    void close() override
    {
        stop();
        opened = false;
    }

    bool isOpen() override                                   { return opened; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (cb == nullptr || callback != nullptr)
            return;

        cb->audioDeviceAboutToStart (this);
        callback = cb;
        startThread();
    }

    void stop() override
    {
        stopThread (2000);

        if (auto* cb = std::exchange (callback, nullptr))
            cb->audioDeviceStopped();
    }

    bool isPlaying() override                                { return callback != nullptr; }
    juce::String getLastError() override                     { return {}; }
    int getCurrentBufferSizeSamples() override               { return 480; }
    double getCurrentSampleRate() override                   { return 48000.0; }
    int getCurrentBitDepth() override                        { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
    int getOutputLatencyInSamples() override                 { return 480; }
    int getInputLatencyInSamples() override                  { return 480; }
    int getXRunCount() const noexcept override               { return 1; }

private:
    void run() override
    {
        juce::AudioBuffer<float> in (2, 480), out (2, 480);
        in.clear();

        while (! threadShouldExit())
        {
            out.clear();
            callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), activeIns.countNumberOfSetBits(),
                                                        out.getArrayOfWritePointers(), activeOuts.countNumberOfSetBits(),
                                                        480, {});
            wait (10);
        }
    }

    juce::BigInteger activeIns, activeOuts;
    juce::AudioIODeviceCallback* volatile callback = nullptr;
    bool opened = false;
};

class BoardDeviceType final : public juce::AudioIODeviceType
{
public:
    BoardDeviceType() : juce::AudioIODeviceType ("Windows Audio") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override  { return { "Headset (2- EarPods)" }; }
    int getDefaultDeviceIndex (bool) const override         { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override       { return false; }

    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override
    {
        return new BoardDevice();
    }
};

/** The reference board's show: three sets, eight songs, five patches. */
inline juce::File makeBoardShow (const juce::File& parent)
{
    auto show = makeDefaultShow ("My Show");
    show.showTemplate = ShowTemplate::band;

    const std::pair<const char*, juce::uint32> patches[] = {
        { "Clean", 0xff4C8DF6 }, { "Clean 2", 0xff4C8DF6 }, { "Crunch", 0xffFF9F0A }, { "Edge", 0xff30C25A }, { "Lead", 0xffFF6B4A },
    };

    // As on the board: stomp 1 turns the amp on and off, EXP 1 moves its
    // output; gate and EQ bypassed, a bypassed delay at the end.
    for (auto& b : show.patches[0].mainLane().blocks)
    {
        if (b.type == "nam")
        {
            b.stomp = 1;
            b.expression["output"] = { 1, -40.0f, 12.0f };
        }
        else if (b.type == "gate" || b.type == "eq")
        {
            b.enabled = false;
        }
    }

    BlockModel delay;
    delay.id = juce::Uuid().toDashedString();
    delay.type = "delay";
    delay.enabled = false;
    show.patches[0].mainLane().blocks.push_back (delay);

    std::map<juce::String, juce::String> ids;
    show.patches[0].name = patches[0].first;
    show.patches[0].colour = patches[0].second;
    ids["Clean"] = show.patches[0].id;

    for (size_t i = 1; i < std::size (patches); ++i)
    {
        auto p = duplicatePatch (show.patches[0], patches[i].first);
        p.colour = patches[i].second;
        ids[p.name] = p.id;
        show.patches.push_back (std::move (p));
    }

    auto cue = [&] (const char* number, const char* name, const char* patch, int pc = -1, bool out = false)
    {
        auto c = RunItem::makeCue (number, name, ids[patch]);
        c.programChange = pc;

        if (out)
            c.midiOut.push_back ({ MidiOutMessage::Kind::programChange, 1, 7, 0 });

        return c;
    };

    auto set1 = RunItem::makeSection ("Set 1", "set");
    set1.children = { cue ("1", "Clean", "Clean"), cue ("2", "Clean 2", "Clean 2"), cue ("3", "Opener", "Crunch", 3),
                      cue ("4", "Ballad in D", "Clean 2") };
    auto set2 = RunItem::makeSection ("Set 2", "set");
    set2.children = { cue ("5", "Shuffle", "Edge"), cue ("6", "Rock Medley", "Lead", 7, true), cue ("7", "Closer", "Crunch") };
    auto encoreSet = RunItem::makeSection ("Encore", "set");
    encoreSet.children = { cue ("8", "Last Call", "Lead") };
    show.runningOrder.children = { set1, set2, encoreSet };

    const auto folder = parent.getChildFile ("My Show");
    ShowFolder::create (folder, show);
    return folder;
}

/** The full-list board's show (a musical): two acts, scenes, bar marks,
    notes, a cut cue, a cue with its own patch, program changes and MIDI out. */
inline juce::File makeMusicalShow (const juce::File& parent)
{
    auto show = makeDefaultShow (juce::String (juce::CharPointer_UTF8 ("Phantom \xe2\x80\x94 tour 2027")));
    show.showTemplate = ShowTemplate::musical;
    std::map<juce::String, juce::String> ids;
    show.patches[0].name = "Clean";
    ids["Clean"] = show.patches[0].id;

    for (auto name : { "Clean 2", "Edge of breakup", "Lead", "Crunch" })
    {
        auto p = duplicatePatch (show.patches[0], name);
        ids[name] = p.id;
        show.patches.push_back (std::move (p));
    }

    auto cue = [&] (const char* name, const char* bar, const char* note, const char* patch, int pc = -1, bool out = false)
    {
        auto c = RunItem::makeCue ({}, name, ids[patch]);
        c.barMark = bar;
        c.note = note;
        c.programChange = pc;

        if (out)
            c.midiOut.push_back ({ MidiOutMessage::Kind::programChange, 1, 7, 0 });

        return c;
    };

    auto prologue = RunItem::makeSection ("Prologue", "scene");
    prologue.children = { cue ("Overture", "bar 1", "Wait for the MD", "Clean"),
                          cue ("Auction", "bar 34 / B", "Swell on the chandelier", "Clean 2", 2) };
    auto rehearsal = RunItem::makeSection ("Rehearsal", "scene");
    auto reprise = cue ("Reprise", "bar 64", "Cut in rehearsal 3", "Edge of breakup");
    reprise.cut = true;
    rehearsal.children = { cue ("Think of Me", "bar 1", "", "Clean"), cue ("Angel of Music", "bar 17 / C", "Capo 2", "Edge of breakup"),
                           reprise, cue ("Title Song", "bar 1 / A", "Big entrance, fade in", "Lead", 5, true),
                           cue ("Title Song coda", "", "", "Lead") };
    auto rooftop = RunItem::makeSection ("Rooftop", "scene");
    auto finale = cue ("Act 1 finale", "bar 88 / F", "Hold for the blackout", "Crunch", -1, true);
    finale.uniquePatch = duplicatePatch (show.patches.back(), "Crunch");
    finale.patchId.clear();
    rooftop.children = { cue ("All I Ask", "bar 1", "Soft, neck pickup", "Clean 2"), finale };
    auto act1 = RunItem::makeSection ("Act 1", "act");
    act1.children = { prologue, rehearsal, rooftop };

    auto masquerade = RunItem::makeSection ("Masquerade", "scene");
    masquerade.children = { cue ("Entr'acte", "", "", "Crunch"), cue ("Masquerade", "bar 1", "", "Lead", 9) };
    auto act2 = RunItem::makeSection ("Act 2", "act");
    act2.children = { masquerade };
    show.runningOrder.children = { act1, act2 };

    const auto folder = parent.getChildFile ("Phantom");
    ShowFolder::create (folder, show);
    return folder;
}

inline juce::XmlElement boardDeviceSetup()
{
    juce::XmlElement setup ("DEVICESETUP");
    setup.setAttribute ("deviceType", "Windows Audio");
    setup.setAttribute ("audioOutputDeviceName", "Headset (2- EarPods)");
    setup.setAttribute ("audioInputDeviceName", "Headset (2- EarPods)");
    setup.setAttribute ("audioDeviceRate", 48000);
    setup.setAttribute ("audioDeviceBufferSize", 480);
    return setup;
}

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
