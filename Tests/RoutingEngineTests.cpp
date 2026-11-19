// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "FakeDevice.h"
#include "TestSignals.h"

#include "engine/AudioEngine.h"
#include "engine/blocks/DelayBlock.h"
#include "engine/core/Realtime.h"
#include "engine/dsp/OutputEq.h"

namespace encore::test
{

namespace
{
    /** Makes a stereo signal from mono: left as it is, right at half and inverted. */
    class WideBlock final : public Block
    {
    public:
        WideBlock() : Block ("testWide", "Wide") {}
        int getOutputChannels (int) const noexcept override { return 2; }

    private:
        void prepareBlock (const ProcessSpec&) override {}
        void resetBlock() noexcept override {}
        void processBlock (float* const* ch, int numChannels, int n, const ProcessContext&) noexcept override
        {
            if (numChannels > 1)
                for (int i = 0; i < n; ++i)
                    ch[1][i] = -0.5f * ch[0][i];
        }
    };

    RoutingOutput output (const juce::String& name, std::vector<int> channels, float levelDb = 0.0f, bool mute = false)
    {
        RoutingOutput o;
        o.name = name;
        o.channels = std::move (channels);
        o.levelDb = levelDb;
        o.mute = mute;
        return o;
    }

    /** Front of house 1/2, In-ear 3/4 at -6 dB, Amp DI 5 (mono); inputs Guitar 1, Acoustic 2. */
    RoutingConfig makeRig()
    {
        RoutingConfig rig;
        rig.inputs = { { "Guitar", 1 }, { "Acoustic", 2 } };
        rig.outputs = { output ("Front of house", { 1, 2 }), output ("In-ear", { 3, 4 }, -6.0f), output ("Amp DI", { 5 }) };
        return rig;
    }
}

class RoutingEngineTests final : public juce::UnitTest
{
public:
    RoutingEngineTests() : juce::UnitTest ("Routing in the engine", "Encore") {}

    /** An engine on an interface, a block at a time. */
    struct Rig
    {
        MultiChannelDevice device;
        AudioEngine engine;
        std::vector<std::vector<float>> ins, outs;

        explicit Rig (int numIns = 4, int numOuts = 6, juce::BigInteger activeIns = {}, juce::BigInteger activeOuts = {})
            : device (numIns, numOuts, activeIns, activeOuts)
        {
            const auto activeInCount = device.getActiveInputChannels().countNumberOfSetBits();
            const auto activeOutCount = device.getActiveOutputChannels().countNumberOfSetBits();
            ins.assign ((size_t) activeInCount, std::vector<float> ((size_t) blockSize));
            outs.assign ((size_t) activeOutCount, std::vector<float> ((size_t) blockSize));
            engine.audioDeviceAboutToStart (&device);
            engine.setMasterGainDecibels (0.0f);
            engine.setMuted (false);
        }

        ~Rig() { engine.audioDeviceStopped(); }

        Chain* play (std::unique_ptr<Chain> chain)
        {
            auto* c = engine.setChain (std::move (chain));
            return c;
        }

        /** One callback with these input samples (per switched-on input). */
        void callback()
        {
            std::vector<const float*> inputs;
            std::vector<float*> outputs;

            for (auto& i : ins)  inputs.push_back (i.data());
            for (auto& o : outs) outputs.push_back (o.data());

            engine.audioDeviceIOCallbackWithContext (inputs.data(), (int) inputs.size(), outputs.data(),
                                                     (int) outputs.size(), blockSize, {});
        }

        void fill (int input, float value)
        {
            std::fill (ins[(size_t) input].begin(), ins[(size_t) input].end(), value);
        }

        /** Settles every ramp (gain, sends, levels: 20 ms or less). */
        void settle (int blocks = 40)
        {
            for (int i = 0; i < blocks; ++i)
                callback();
        }

        float out (int index, int sample = blockSize - 1) const { return outs[(size_t) index][(size_t) sample]; }
    };

    static std::unique_ptr<Chain> emptyChain()  { return std::make_unique<Chain>(); }

    void runTest() override
    {
        beginTest ("The default routing plays exactly as before: input 1 to every output");
        {
            Rig rig (2, 4);
            rig.play (emptyChain());
            rig.fill (0, 0.25f);
            rig.fill (1, 0.5f);   // input 2 isn't the guitar
            rig.settle();

            for (int o = 0; o < 4; ++o)
                expectWithinAbsoluteError (rig.out (o), 0.25f, 1.0e-4f);

            expectWithinAbsoluteError (rig.engine.getInputMeter().take(), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (rig.engine.getOutputMeter().take(), 0.25f, 1.0e-4f);
        }

        beginTest ("Named outputs: channels, level, mute, a mono output");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            rig.play (emptyChain());
            rig.fill (0, 0.25f);
            rig.settle();

            expectWithinAbsoluteError (rig.out (0), 0.25f, 1.0e-4f, "front of house left");
            expectWithinAbsoluteError (rig.out (1), 0.25f, 1.0e-4f, "front of house right");
            expectWithinAbsoluteError (rig.out (2), 0.25f * juce::Decibels::decibelsToGain (-6.0f), 1.0e-4f, "in-ear at -6 dB");
            expectWithinAbsoluteError (rig.out (4), 0.25f, 1.0e-4f, "amp DI, mono");
            expectEquals (rig.out (5), 0.0f, "output 6 isn't used");

            const auto di = rig.engine.getOutputSlot ("amp di");
            expect (di >= 0);
            rig.engine.setOutputMuted (di, true);
            rig.callback();
            expect (std::abs (rig.out (4, 0)) > 0.0f && std::abs (rig.out (4, 0)) < 0.25f, "a mute fades, it doesn't click");
            rig.settle();
            expectEquals (rig.out (4), 0.0f);
            expectWithinAbsoluteError (rig.out (0), 0.25f, 1.0e-4f, "the others play on");
            expect (rig.engine.isOutputMuted (di));
            expect (rig.engine.getOutputSlot ("Monitor wedge") < 0);
        }

        beginTest ("The big mute and panic still silence every output");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            rig.play (emptyChain());
            rig.fill (0, 0.25f);
            rig.settle();
            rig.engine.setMuted (true);
            rig.settle();

            for (int o = 0; o < 6; ++o)
                expectEquals (rig.out (o), 0.0f);
        }

        beginTest ("Sends: per output, ramped, an unrouted output gets nothing");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            auto* chain = rig.play (emptyChain());
            rig.fill (0, 0.25f);

            rig.engine.applyPatchRouting (*chain, "Guitar", [] (const juce::String& outputName)
            {
                return outputName == "In-ear" ? 0.0f : routing::gainFor (-6.0f);
            });

            rig.settle();
            expectWithinAbsoluteError (rig.out (0), 0.25f * routing::gainFor (-6.0f), 1.0e-4f);
            expectEquals (rig.out (2), 0.0f, "send off");

            // Back up to 0 dB: a ramp, not a jump.
            rig.engine.applyPatchRouting (*chain, "Guitar", [] (const juce::String&) { return 1.0f; });
            rig.callback();
            const auto& foh = rig.outs[0];
            float biggestStep = 0.0f;

            for (int i = 1; i < blockSize; ++i)
                biggestStep = juce::jmax (biggestStep, std::abs (foh[(size_t) i] - foh[(size_t) i - 1]));

            expect (biggestStep < 0.002f, "the send ramps (biggest step " + juce::String (biggestStep) + ")");
            rig.settle();
            expectWithinAbsoluteError (rig.out (0), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (rig.out (2), 0.25f * routing::gainFor (-6.0f), 1.0e-4f);
        }

        beginTest ("Each patch plays its own input; a name not here plays the first");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            auto* chain = rig.play (emptyChain());
            rig.fill (0, 0.1f);
            rig.fill (1, 0.3f);

            rig.engine.applyPatchRouting (*chain, "acoustic", nullptr);
            rig.settle();
            expectWithinAbsoluteError (rig.out (0), 0.3f, 1.0e-4f);
            expectWithinAbsoluteError (rig.engine.getInputMeter().take(), 0.3f, 1.0e-4f, "the meter follows the patch's input");
            expectWithinAbsoluteError (rig.engine.getInputSlotMeter (rig.engine.getInputSlot ("Guitar")).take(), 0.1f, 1.0e-4f);

            rig.engine.applyPatchRouting (*chain, "Mic", nullptr);
            rig.settle();
            expectWithinAbsoluteError (rig.out (0), 0.1f, 1.0e-4f, "Mic isn't here: the first input");
        }

        beginTest ("Channels the interface doesn't have or hasn't switched on are skipped");
        {
            juce::BigInteger activeOuts;
            activeOuts.setBit (0);
            activeOuts.setBit (1);
            activeOuts.setBit (3);   // outputs 1, 2 and 4 on; 3 off
            Rig rig (1, 4, {}, activeOuts);

            auto config = makeRig();   // in-ear 3/4, amp DI 5
            rig.engine.setRouting (config);
            rig.play (emptyChain());
            rig.fill (0, 0.25f);
            rig.settle();

            expectEquals ((int) rig.outs.size(), 3);
            expectWithinAbsoluteError (rig.out (0), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (rig.out (2), 0.25f * routing::gainFor (-6.0f), 1.0e-4f, "output 4 (the third switched on) gets in-ear right");
            expect (rig.engine.isOutputChannelUsable (4));
            expect (! rig.engine.isOutputChannelUsable (3));
            expect (! rig.engine.isOutputChannelUsable (5));
            expectEquals (rig.engine.getDeviceOutputChannels(), 4);
            expect (rig.engine.isInputChannelUsable (1));
            expect (! rig.engine.isInputChannelUsable (2), "Acoustic's input 2 isn't there: silence");
        }

        beginTest ("Two outputs on one channel add up");
        {
            Rig rig (1, 2);
            RoutingConfig config;
            config.inputs = { { "Guitar", 1 } };
            config.outputs = { output ("A", { 1 }), output ("B", { 1, 2 }) };
            rig.engine.setRouting (config);
            rig.play (emptyChain());
            rig.fill (0, 0.2f);
            rig.settle();
            expectWithinAbsoluteError (rig.out (0), 0.4f, 1.0e-4f);
            expectWithinAbsoluteError (rig.out (1), 0.2f, 1.0e-4f);
        }

        beginTest ("Stereo: a stereo block stays stereo; a mono output gets (L + R) / 2");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            auto chain = emptyChain();
            chain->addBlock (std::make_unique<WideBlock>());
            rig.play (std::move (chain));
            rig.fill (0, 0.4f);
            rig.settle();

            expectWithinAbsoluteError (rig.out (0), 0.4f, 1.0e-4f);
            expectWithinAbsoluteError (rig.out (1), -0.2f, 1.0e-4f);
            expectWithinAbsoluteError (rig.out (4), 0.1f, 1.0e-4f, "mono: (0.4 - 0.2) / 2");
        }

        beginTest ("Output EQ: the low cut removes lows, flat changes nothing");
        {
            Rig rig (1, 2);
            auto config = makeDefaultRouting();
            config.outputs = { output ("PA", { 1, 2 }) };
            config.outputs[0].eq.lowCut = 400.0f;
            rig.engine.setRouting (config);
            rig.play (emptyChain());

            const auto low = sine (50.0, 0.3f, blockSize * 200);
            float peakOut = 0.0f;

            for (int b = 0; b < 200; ++b)
            {
                std::copy (low.begin() + b * blockSize, low.begin() + (b + 1) * blockSize, rig.ins[0].begin());
                rig.callback();

                if (b > 100)
                    peakOut = juce::jmax (peakOut, peak (rig.outs[0], 0, blockSize));
            }

            expect (peakOut < 0.3f * 0.05f, "50 Hz through a 400 Hz low cut: " + juce::String (toDb (peakOut / 0.3f), 1) + " dB");

            OutputEq flat;
            expect (std::abs (dsp::outputEq::responseDb (flat, 1000.0)) < 0.01);
            OutputEq mid;
            mid.mid = 6.0f;
            mid.midFreq = 1000.0f;
            expectWithinAbsoluteError (dsp::outputEq::responseDb (mid, 1000.0), 6.0, 0.05);
        }

        beginTest ("Output delay: lines up a far PA, sample exact, crossfades a change");
        {
            Rig rig (1, 2);
            auto config = makeDefaultRouting();
            config.outputs = { output ("PA", { 1 }), output ("Near", { 2 }) };
            config.outputs[0].delayMs = 10.0f;   // 480 samples at 48 kHz
            rig.engine.setRouting (config);
            rig.play (emptyChain());
            rig.settle();

            std::vector<float> pa, near;

            for (int b = 0; b < 8; ++b)
            {
                std::fill (rig.ins[0].begin(), rig.ins[0].end(), 0.0f);

                if (b == 0)
                    rig.ins[0][0] = 0.5f;

                rig.callback();
                pa.insert (pa.end(), rig.outs[0].begin(), rig.outs[0].end());
                near.insert (near.end(), rig.outs[1].begin(), rig.outs[1].end());
            }

            auto loudest = [] (const std::vector<float>& s)
            {
                return (int) std::distance (s.begin(), std::max_element (s.begin(), s.end(),
                                                                         [] (float a, float b) { return std::abs (a) < std::abs (b); }));
            };

            expectEquals (loudest (near), 0);
            expectEquals (loudest (pa), 480);
            expectWithinAbsoluteError (pa[480], 0.5f, 1.0e-4f);

            // A change while a tone plays: crossfaded, no hole and no spike.
            config.outputs[0].delayMs = 20.0f;
            rig.fill (0, 0.25f);
            rig.settle (10);
            rig.engine.setRouting (config);
            float lowest = 1.0f;

            for (int b = 0; b < 10; ++b)
            {
                rig.callback();
                lowest = juce::jmin (lowest, *std::min_element (rig.outs[0].begin(), rig.outs[0].end()));
            }

            expect (lowest > 0.24f, "a steady tone stays steady through a delay change");
        }

        beginTest ("Each output has its own safety limiter");
        {
            Rig rig (1, 4);
            RoutingConfig config;
            config.inputs = { { "Guitar", 1 } };
            config.outputs = { output ("Hot", { 1, 2 }, 6.0f), output ("Quiet", { 3, 4 }, -12.0f) };
            rig.engine.setRouting (config);
            rig.play (emptyChain());
            rig.fill (0, 0.8f);
            rig.settle();

            const auto ceiling = juce::Decibels::decibelsToGain (dsp::SafetyLimiter::ceilingDb);
            expect (rig.out (0) <= ceiling + 1.0e-4f, "the hot output is limited");
            expectWithinAbsoluteError (rig.out (2), 0.8f * routing::gainFor (-12.0f), 1.0e-4f, "the quiet one isn't ducked by it");
            expect (rig.engine.takeOutputLimiterReductionDb (rig.engine.getOutputSlot ("Hot")) < -1.0f);
            expect (rig.engine.takeOutputLimiterReductionDb (rig.engine.getOutputSlot ("Quiet")) > -0.01f);
            expect (rig.engine.takeLimiterReductionDb() <= 0.0f);
        }

        beginTest ("Outputs keep their slot by name: reordering moves nothing");
        {
            Rig rig;
            auto config = makeRig();
            rig.engine.setRouting (config);
            const auto inEar = rig.engine.getOutputSlot ("In-ear");
            auto* chain = rig.play (emptyChain());
            rig.engine.applyPatchRouting (*chain, {}, [] (const juce::String& n) { return n == "In-ear" ? 0.0f : 1.0f; });
            rig.fill (0, 0.25f);
            rig.settle();

            std::swap (config.outputs[0], config.outputs[2]);
            rig.engine.setRouting (config);
            expectEquals (rig.engine.getOutputSlot ("In-ear"), inEar);
            rig.callback();
            expectEquals (rig.out (2, 0), 0.0f, "the send off still goes to in-ear, from the first sample");
            expectWithinAbsoluteError (rig.out (0, 0), 0.25f, 1.0e-4f, "and front of house doesn't fade or jump");

            // A renamed output is a new one: it fades in from silence.
            config.outputs[0].name = "Monitor";
            rig.engine.setRouting (config);
            rig.engine.applyPatchRouting (*chain, {}, nullptr);
            rig.callback();
            expect (rig.out (4, 0) < 0.25f, "a new output fades in");
        }

        beginTest ("Panic clears the outputs' delay lines too");
        {
            Rig rig (1, 2);
            auto config = makeDefaultRouting();
            config.outputs = { output ("PA", { 1, 2 }) };
            config.outputs[0].delayMs = 100.0f;
            rig.engine.setRouting (config);
            rig.play (emptyChain());
            rig.fill (0, 0.3f);
            rig.settle (60);
            rig.fill (0, 0.0f);
            rig.engine.panic();

            float after = 0.0f;

            for (int b = 0; b < 160; ++b)
            {
                rig.callback();

                if (rig.engine.getPanicState() == AudioEngine::PanicState::returning
                    || rig.engine.getPanicState() == AudioEngine::PanicState::none)
                    after = juce::jmax (after, peak (rig.outs[0], 0, blockSize));
            }

            expectEquals (after, 0.0f, "nothing from before the panic comes back out of the delay");
        }

        beginTest ("A ringing patch keeps its own sends");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            auto& player = rig.engine.getPlayer();

            auto echo = emptyChain();
            auto& delay = echo->addBlock (std::make_unique<DelayBlock>());
            delay.findParameter ("time")->setValue (100.0f);
            delay.findParameter ("feedback")->setValue (60.0f);
            delay.findParameter ("mix")->setValue (100.0f);
            auto* a = player.adopt (std::move (echo));
            auto* b = player.adopt (emptyChain());
            player.publish ({ { a }, { b } });

            rig.engine.applyPatchRouting (*a, {}, [] (const juce::String& n) { return n == "Front of house" ? 1.0f : 0.0f; });
            rig.engine.applyPatchRouting (*b, {}, [] (const juce::String& n) { return n == "In-ear" ? 1.0f : 0.0f; });

            player.goToCue (0);
            rig.fill (0, 0.25f);
            rig.settle (60);
            player.goToCue (1);
            rig.fill (0, 0.0f);   // stop playing: only a's echo should sound now
            rig.settle (40);

            float fohTail = 0.0f, inEar = 0.0f;

            for (int i = 0; i < 20; ++i)
            {
                rig.callback();
                fohTail = juce::jmax (fohTail, peak (rig.outs[0], 0, blockSize));
                inEar = juce::jmax (inEar, peak (rig.outs[2], 0, blockSize));
            }

            expect (fohTail > 0.01f, "a's echo rings out in front of house");
            expectEquals (inEar, 0.0f, "and never in b's in-ear");
        }

        beginTest ("Routing, sends and switching never allocate on the audio thread");
        {
            Rig rig;
            rig.engine.setRouting (makeRig());
            auto& player = rig.engine.getPlayer();
            auto* a = player.adopt (emptyChain());
            auto wide = emptyChain();
            wide->addBlock (std::make_unique<WideBlock>());
            wide->addBlock (std::make_unique<DelayBlock>());
            auto* b = player.adopt (std::move (wide));
            player.publish ({ { a }, { b } });
            rig.fill (0, 0.2f);
            rig.settle (4);   // the first table is adopted outside the count

            auto config = makeRig();
            const auto before = realtimeAllocationCount();

            for (int i = 0; i < 400; ++i)
            {
                if (i % 50 == 0)
                {
                    // Message-thread work between callbacks, as the UI does it.
                    config.outputs[1].eq.bass = (float) (i % 100) / 10.0f;
                    config.outputs[0].delayMs = (float) (i % 7);
                    rig.engine.setRouting (config);
                    rig.engine.collectGarbage();
                    rig.engine.setOutputMuted (rig.engine.getOutputSlot ("Amp DI"), i % 100 == 0);
                    rig.engine.applyPatchRouting (*b, "Acoustic", [i] (const juce::String&) { return (float) (i % 3) * 0.5f; });
                }

                if (i % 9 == 0)
                    player.goToCue ((i / 9) % 2);

                rig.callback();
            }

            expectEquals (realtimeAllocationCount() - before, 0);
        }

        beginTest ("The mono path (tests, simple setups) still plays");
        {
            PatchPlayer player;
            player.prepareAll (spec);
            auto chain = emptyChain();
            chain->addBlock (std::make_unique<WideBlock>());
            auto* c = player.adopt (std::move (chain));
            player.publish ({});
            player.play (c);

            std::vector<float> io ((size_t) blockSize, 0.4f);

            for (int i = 0; i < 20; ++i)
            {
                std::fill (io.begin(), io.end(), 0.4f);
                player.process (io.data(), blockSize);
            }

            expectWithinAbsoluteError (io.back(), 0.1f, 1.0e-4f, "stereo as (L + R) / 2");
        }
    }
};

static RoutingEngineTests routingEngineTests;

} // namespace encore::test
