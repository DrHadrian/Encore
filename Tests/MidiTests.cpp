// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/EqBlock.h"
#include "engine/core/Realtime.h"
#include "engine/midi/MidiRouter.h"

namespace encore::test
{

class MidiRouterTests final : public juce::UnitTest
{
public:
    MidiRouterTests() : juce::UnitTest ("MIDI foot control", "Encore") {}

    struct Rig
    {
        PatchPlayer player;
        MidiRouter router { false };
        std::atomic<bool> muted { false };
        std::vector<Chain*> chains;
        std::vector<float> buffer = std::vector<float> ((size_t) blockSize, 0.0f);

        Rig()
        {
            player.prepareAll (spec);

            for (int i = 0; i < 3; ++i)
            {
                auto chain = std::make_unique<Chain>();
                auto& eq = chain->addBlock (std::make_unique<EqBlock>());
                eq.setStomp (1);
                chain->addExpressionTarget (1, *eq.findParameter ("bass"), -12.0f, 12.0f);
                chains.push_back (player.adopt (std::move (chain)));
            }

            player.publish ({ { chains[0] }, { chains[1] }, { chains[2], false, 7 } });
        }

        /** What one audio callback does with MIDI. */
        void callback()
        {
            router.process (player, muted);
            player.process (buffer.data(), blockSize);
        }

        void cc (int number, int value, int channel = 1)   { router.receive ((juce::uint8) (0xb0 | (channel - 1)), (juce::uint8) number, (juce::uint8) value); }
        void note (int number, int channel = 1)            { router.receive ((juce::uint8) (0x90 | (channel - 1)), (juce::uint8) number, 100); }
        void program (int number, int channel = 1)         { router.receive ((juce::uint8) (0xc0 | (channel - 1)), (juce::uint8) number, 0); }
    };

    void runTest() override
    {
        beginTest ("A footswitch steps once per press, never while held");
        {
            Rig rig;
            rig.cc (64, 127);
            rig.callback();
            expectEquals (rig.player.getCurrentCue(), 0);

            rig.cc (64, 127);   // still held (some pedals repeat)
            rig.callback();
            expectEquals (rig.player.getCurrentCue(), 0);

            rig.cc (64, 0);
            rig.cc (64, 127);
            rig.callback();
            expectEquals (rig.player.getCurrentCue(), 1);
            expect (rig.player.getActiveChain() == rig.chains[1]);
        }

        beginTest ("Program change recalls the cue marked with it, else by position");
        {
            Rig rig;
            rig.program (7);
            rig.callback();
            expectEquals (rig.player.getCurrentCue(), 2, "the cue marked PC 7");

            rig.program (1);
            rig.callback();
            expectEquals (rig.player.getCurrentCue(), 1, "no cue marked PC 1: the second cue");
        }

        beginTest ("Stomp switches toggle the patch's assigned blocks");
        {
            Rig rig;
            rig.router.setBindings ({ { MidiAction::stomp, 1, { MidiTrigger::Kind::note, 0, 36 } } });
            rig.player.goToCue (0);
            rig.callback();

            auto& eq = *rig.chains[0]->getBlocks()[0];
            expect (eq.isEnabled());
            rig.note (36);
            rig.callback();
            expect (! eq.isEnabled(), "off");
            rig.note (36);
            rig.callback();
            expect (eq.isEnabled(), "on again");
        }

        beginTest ("Expression pedals move their knobs, and the next patch picks up the position");
        {
            Rig rig;
            rig.player.goToCue (0);
            rig.callback();

            rig.cc (11, 127);   // default: CC 11 = expression 1 (toe down)
            rig.callback();
            expectEquals (rig.chains[0]->getBlocks()[0]->findParameter ("bass")->getValue(), 12.0f);

            rig.cc (11, 0);   // heel
            rig.callback();
            expectEquals (rig.chains[0]->getBlocks()[0]->findParameter ("bass")->getValue(), -12.0f);
            expectEquals (rig.router.getPedalPosition (1), 0.0f);

            rig.player.goToCue (1);
            rig.callback();   // switches
            rig.callback();   // the router sees the new patch
            expectEquals (rig.chains[1]->getBlocks()[0]->findParameter ("bass")->getValue(), -12.0f,
                          "the new patch starts at the pedal's position");
        }

        beginTest ("Mute, and channel filtering");
        {
            Rig rig;
            rig.router.setBindings ({ { MidiAction::mute, 1, { MidiTrigger::Kind::controlChange, 2, 20 } } });
            rig.cc (20, 127, 1);   // wrong channel
            rig.callback();
            expect (! rig.muted.load());
            rig.cc (20, 127, 2);
            rig.callback();
            expect (rig.muted.load());
        }

        beginTest ("The last message is available for MIDI learn");
        {
            Rig rig;
            const auto before = rig.router.getLastReceived().sequence;
            rig.cc (80, 127, 3);
            const auto last = rig.router.getLastReceived();
            expectGreaterThan (last.sequence, before);
            expect (last.isPress());
            expect (last.toTrigger() == MidiTrigger { MidiTrigger::Kind::controlChange, 3, 80 });
            expect (last.describe().contains ("CC 80"));
        }

        beginTest ("Entering a cue sends its MIDI out, whatever caused the change");
        {
            Rig rig;
            rig.router.setCueMidiOut ({ {}, { { MidiOutMessage::Kind::programChange, 1, 5, 0 },
                                              { MidiOutMessage::Kind::controlChange, 2, 20, 127 } }, {} });
            rig.player.goToCue (1);   // a click, not MIDI
            rig.callback();
            rig.callback();

            std::vector<juce::MidiMessage> sent;
            rig.router.sendPendingCueMessages ([&] (const juce::MidiMessage& m) { sent.push_back (m); });
            expectEquals ((int) sent.size(), 2);

            if (sent.size() == 2)
            {
                expect (sent[0].isProgramChange() && sent[0].getProgramChangeNumber() == 5 && sent[0].getChannel() == 1);
                expect (sent[1].isController() && sent[1].getControllerNumber() == 20 && sent[1].getChannel() == 2);
            }
        }

        beginTest ("Handling MIDI never allocates on the audio thread");
        {
            Rig rig;
            const auto before = realtimeAllocationCount();

            for (int i = 0; i < 2000; ++i)
            {
                rig.cc (64, i % 2 == 0 ? 127 : 0);
                rig.cc (11, i % 128);
                rig.program (i % 3);

                const rt::ScopedRealtimeSection realtime;
                rig.callback();
            }

            expectEquals (realtimeAllocationCount() - before, 0);
        }

        beginTest ("Several devices sending at once is safe");
        {
            Rig rig;
            std::atomic<bool> stop { false };
            std::vector<std::unique_ptr<std::thread>> devices;

            for (int d = 0; d < 4; ++d)
                devices.push_back (std::make_unique<std::thread> ([&rig, &stop, d]
                {
                    for (int i = 0; i < 2000 && ! stop.load(); ++i)
                        rig.cc (11, (i + d) % 128, d + 1);
                }));

            for (int i = 0; i < 500; ++i)
                rig.callback();

            stop.store (true);

            for (auto& t : devices)
                t->join();

            rig.callback();
            expect (true, "no crash, no corruption");
        }
    }
};

static MidiRouterTests midiRouterTests;

} // namespace encore::test
