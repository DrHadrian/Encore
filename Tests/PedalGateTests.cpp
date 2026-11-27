// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2: the Pedal Gate "S2" (GATE-PLAN §2).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalGateBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

class PedalGateTests final : public juce::UnitTest
{
public:
    PedalGateTests() : juce::UnitTest ("Pedal Gate", "Encore") {}

    /** Runs a block over mono (or stereo) signals, keyed context optional. */
    static std::vector<std::vector<float>> process (Block& block, std::vector<std::vector<float>> io,
                                                    const std::vector<float>* chainInput = nullptr)
    {
        const auto n = (int) io[0].size();

        for (int offset = 0; offset < n; offset += blockSize)
        {
            const auto len = juce::jmin (blockSize, n - offset);
            float* ch[2] = { io[0].data() + offset, io.size() > 1 ? io[1].data() + offset : nullptr };
            ProcessContext context;

            if (chainInput != nullptr)
                context.chainInput = chainInput->data() + offset;

            block.process (ch, (int) io.size(), len, context);
        }

        return io;
    }

    void runTest() override
    {
        beginTest ("Parameters: ids, ranges and defaults as GATE-PLAN §2");
        {
            PedalGateBlock s2;
            expectEquals (s2.getTypeId(), juce::String ("pedalGate"));
            expectEquals ((int) s2.getParameters().size(), 4);

            const auto* threshold = s2.findParameter ("threshold");
            const auto* decay = s2.findParameter ("decay");
            const auto* detect = s2.findParameter ("detect");
            const auto* soft = s2.findParameter ("soft");
            expect (threshold != nullptr && decay != nullptr && detect != nullptr && soft != nullptr);

            if (threshold == nullptr || decay == nullptr || detect == nullptr || soft == nullptr)
                return;

            expectEquals (threshold->getSpec().range.start, 0.0f);
            expectEquals (threshold->getSpec().range.end, 10.0f);
            expectEquals (threshold->getValue(), 3.0f);
            expectEquals (decay->getValue(), 4.0f);
            expect (detect->isChoice() && detect->getChoiceIndex() == (int) PedalGateBlock::Detect::input);
            expect (soft->isChoice() && soft->getChoiceIndex() == (int) PedalGateBlock::Mode::hard);
            expect (detect->getSpec().choices == juce::StringArray ({ "Input", "Guitar" }));
            expect (soft->getSpec().choices == juce::StringArray ({ "Hard", "Soft" }));

            expectEquals (PedalGateBlock::offsetDbFor (0.0f), 0.0f);
            expectEquals (PedalGateBlock::offsetDbFor (10.0f), 30.0f);
            expectWithinAbsoluteError (PedalGateBlock::releaseMsFor (0.0f), 20.0f, 1.0e-4f);
            expectWithinAbsoluteError (PedalGateBlock::releaseMsFor (4.0f), 87.5f, 0.5f);
            expectWithinAbsoluteError (PedalGateBlock::releaseMsFor (10.0f), 800.0f, 1.0e-2f);

            expect (! s2.hasTail());
            expectEquals (s2.getLatencySamples(), 0);
        }

        beginTest ("Starts open, keeps the channels it gets (mono and stereo)");
        {
            for (int channels : { 1, 2 })
            {
                PedalGateBlock s2;
                s2.prepare (spec);
                s2.reset();
                expect (s2.isOpen(), "starts open: switched on mid-note it doesn't mute");
                expectEquals (s2.getOutputChannels (channels), channels);

                auto note = pluck (48000, 0, 0.5f);
                std::vector<std::vector<float>> io ((size_t) channels, note.signal);

                if (channels == 2)
                    for (auto& x : io[1])
                        x *= 0.5f;

                const auto out = process (s2, io);
                expectWithinAbsoluteError (out[0][100], note.signal[100], 1.0e-6f, "open on the note");

                if (channels == 2)
                    expectWithinAbsoluteError (out[1][100], note.signal[100] * 0.5f, 1.0e-6f, "both sides, linked");
            }
        }

        beginTest ("It closes on the noise after a note, and opens on the next pick within 1 dB");
        {
            PedalGateBlock s2;
            s2.prepare (spec);
            s2.reset();
            s2.setLearnedFloorDb (-70.0f);
            auto note = pluck (48000 * 2, 48000, 0.5f, 2000.0, -70.0f);
            const auto out = process (s2, { note.signal });

            expect (rms (out[0], 40000, 47000) < rms (note.signal, 40000, 47000) * 0.01f, "noise gated before the note");
            expect (! std::isnan (out[0][47000]));
            expectWithinAbsoluteError (toDb (peak (out[0], 48000, 48240)), toDb (peak (note.signal, 48000, 48240)), 1.0f,
                                       "the pick attack");
            expect (s2.getGainReductionDb() < 1.0f && s2.isOpen(), "open while the note rings");
        }

        beginTest ("Detect Guitar keys from the clean input while it cuts where it sits");
        {
            // Where it sits: a steady amp hiss at -40 dBFS. The clean guitar:
            // a pluck that ends. Input would keep it open on the hiss.
            const int n = 48000 + 24000;
            auto hiss = noise (juce::Decibels::decibelsToGain (-40.0f), n, 5);
            auto clean = pluck (n, 0, 0.5f, 200.0, -90.0f);

            PedalGateBlock onInput, onGuitar;
            onGuitar.findParameter ("detect")->setValue ((float) PedalGateBlock::Detect::guitar);

            for (auto* s2 : { &onInput, &onGuitar })
            {
                s2->prepare (spec);
                s2->reset();
                s2->setLearnedFloorDb (-70.0f);
            }

            const auto a = process (onInput, { hiss }, &clean.signal);
            const auto b = process (onGuitar, { hiss }, &clean.signal);
            expect (rms (a[0], n - 4800, n) > rms (hiss, n - 4800, n) * 0.9f, "Input: open on the hiss");
            expect (rms (b[0], n - 4800, n) < rms (hiss, n - 4800, n) * 1.0e-3f, "Guitar: closed once the note ends");
        }

        beginTest ("Soft: an expander 1:2, at most 15 dB down (static curve)");
        {
            // Below -85 dBFS learning is frozen, so the floor stays where it's set.
            for (auto [levelDb, expectedDb] : { std::pair { -89.0f, 2.0f }, std::pair { -97.0f, 10.0f }, std::pair { -110.0f, 15.0f } })
            {
                PedalGateBlock s2;
                s2.findParameter ("soft")->setValue ((float) PedalGateBlock::Mode::soft);
                s2.prepare (spec);
                s2.reset();
                s2.setLearnedFloorDb (-96.0f);   // threshold -87 (knob 3: +9 dB)

                std::vector<float> steady (48000, juce::Decibels::decibelsToGain (levelDb, -200.0f));
                const auto out = process (s2, { steady });
                expectWithinAbsoluteError (toDb (out[0].back()) - levelDb, -expectedDb, 0.05f,
                                           juce::String (levelDb) + " dB in");
            }
        }

        beginTest ("Factory presets: each loads with its stated values");
        {
            PedalGateBlock s2;
            const auto list = s2.getFactoryPresets();
            expectEquals ((int) list.size(), 4);

            struct Expected { const char* id; float t, d; int detect, soft; };
            const Expected expected[] { { "cleanHiss", 2, 6, 0, 1 }, { "singleCoils", 3, 5, 0, 0 },
                                        { "highGain", 4, 3, 1, 0 }, { "djentTight", 5, 1, 1, 0 } };

            for (const auto& e : expected)
            {
                const auto* preset = presets::find (s2, e.id);
                expect (preset != nullptr, e.id);

                if (preset == nullptr)
                    continue;

                presets::apply (s2, *preset);
                expectEquals (s2.findParameter ("threshold")->getValue(), e.t, e.id);
                expectEquals (s2.findParameter ("decay")->getValue(), e.d, e.id);
                expectEquals (s2.findParameter ("detect")->getChoiceIndex(), e.detect, e.id);
                expectEquals (s2.findParameter ("soft")->getChoiceIndex(), e.soft, e.id);

                for (const auto& p : s2.getParameters())
                    expect (s2.isPresetParameter (*p), "every parameter is a preset parameter");
            }
        }

        beginTest ("Switching Detect or Mode while playing crossfades, no jump");
        {
            PedalGateBlock s2;
            s2.prepare (spec);
            s2.reset();
            auto tone = sine (220.0, 0.3f, 48000);
            std::vector<float> quiet (48000, 0.0f);
            auto maxStep = 0.0f;
            std::vector<float> previous { 0.0f };

            for (int offset = 0; offset < 48000; offset += blockSize)
            {
                if (offset == 24000 - (24000 % blockSize))
                    s2.findParameter ("detect")->setValue (1.0f);   // to the guitar: silent

                float* ch[1] = { tone.data() + offset };
                ProcessContext context;
                context.chainInput = quiet.data() + offset;
                s2.process (ch, 1, blockSize, context);
            }

            for (int i = 1; i < 48000; ++i)
                maxStep = juce::jmax (maxStep, std::abs (tone[(size_t) i] - tone[(size_t) i - 1]));

            // A 220 Hz sine of 0.3 moves at most 2 pi f A / fs = 0.0086 per sample.
            expect (maxStep < 0.0087f, "no jump: " + juce::String (maxStep));
        }

        beginTest ("The UI's state: OPEN, gain reduction, the floor, frozen");
        {
            PedalGateBlock s2;
            s2.prepare (spec);
            s2.reset();
            expectEquals (s2.getLearnedFloorDb(), dsp::AdaptiveGate::initialFloorDb);
            process (s2, { noise (juce::Decibels::decibelsToGain (-90.0f), 48000) });
            expect (! s2.isOpen());
            expect (s2.getGainReductionDb() > 80.0f);
            expect (s2.isLearningFrozen(), "below -85 dBFS");
        }

        beginTest ("No allocation while playing and switching");
        {
            PedalGateBlock s2;
            s2.prepare (spec);
            s2.reset();
            auto note = pluck (48000, 4800, 0.5f);
            auto* soft = s2.findParameter ("soft");
            auto* detect = s2.findParameter ("detect");
            auto* threshold = s2.findParameter ("threshold");
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int offset = 0; offset + blockSize <= 48000; offset += blockSize)
                {
                    if (offset % 4096 == 0)
                    {
                        soft->setValue ((float) ((offset / 4096) % 2));
                        detect->setValue ((float) ((offset / 8192) % 2));
                        threshold->setValue ((float) ((offset / 4096) % 11));
                    }

                    float* ch[1] = { note.signal.data() + offset };
                    ProcessContext context;
                    context.chainInput = note.signal.data() + offset;
                    s2.process (ch, 1, blockSize, context);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static PedalGateTests pedalGateTests;

} // namespace encore::test
