// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/NamAmpBlock.h"
#include "engine/blocks/NoiseGateBlock.h"
#include "engine/core/Chain.h"
#include "engine/core/Realtime.h"
#include "engine/nam/NamModel.h"

namespace encore::test
{

inline juce::File exampleModel (const char* name)
{
    return juce::File (ENCORE_NAM_EXAMPLE_MODELS).getChildFile (name);
}

class NamModelTests final : public juce::UnitTest
{
public:
    NamModelTests() : juce::UnitTest ("NAM models", "Encore") {}

    void runTest() override
    {
        beginTest ("A standard WaveNet capture loads, plays and reports its cost");
        {
            juce::String error;
            auto model = NamModel::load (exampleModel ("wavenet_a1_standard.nam"), spec, error);
            expect (model != nullptr, error);

            if (model != nullptr)
            {
                expect (! model->isResampling(), "48 kHz model on a 48 kHz device");
                expectEquals (model->getLatencySamples(), 0);
                expectGreaterThan (model->getMeasuredCpuLoad(), 0.0);

                auto signal = sine (110.0, 0.3f, 24000);
                for (int offset = 0; offset < 24000; offset += blockSize)
                {
                    const auto n = juce::jmin (blockSize, 24000 - offset);
                    model->process (signal.data() + offset, signal.data() + offset, n);
                }

                bool finite = true;
                for (auto x : signal)
                    finite = finite && std::isfinite (x);

                expect (finite);
                expectGreaterThan (rms (signal, 12000, 24000), 1.0e-4f, "the amp makes sound");
            }
        }

        beginTest ("A 48 kHz capture on a 44.1 kHz device is converted, with latency reported");
        {
            juce::String error;
            auto model = NamModel::load (exampleModel ("lstm.nam"), { 44100.0, blockSize }, error);
            expect (model != nullptr, error);

            if (model != nullptr)
            {
                expect (model->isResampling());
                expectEquals (model->getModelRate(), 48000.0);
                expectGreaterThan (model->getLatencySamples(), 0);
                expectLessThan (model->getLatencySamples(), 45, "under 1 ms");
            }
        }

        beginTest ("Missing and broken files fail cleanly with a message");
        {
            juce::String error;
            expect (NamModel::load (juce::File::getCurrentWorkingDirectory().getChildFile ("nope.nam"), spec, error) == nullptr);
            expect (error.isNotEmpty());

            const juce::TemporaryFile temp (".nam");
            temp.getFile().replaceWithText ("this is not a model");
            expect (NamModel::load (temp.getFile(), spec, error) == nullptr);
            expect (error.isNotEmpty());
        }
    }
};

class NamAmpBlockTests final : public juce::UnitTest
{
public:
    NamAmpBlockTests() : juce::UnitTest ("NAM amp block", "Encore") {}

    static std::unique_ptr<NamModel> loadModel (const char* name)
    {
        juce::String error;
        auto model = NamModel::load (exampleModel (name), spec, error);
        jassert (model != nullptr);
        return model;
    }

    void runTest() override
    {
        beginTest ("Without a model the guitar passes through");
        {
            NamAmpBlock amp;
            amp.prepare (spec);
            const auto input = noise (0.3f, 4800);
            const auto output = runMono (amp, input);
            expectLessThan (peak (output, 0, 4800) - peak (input, 0, 4800), 1.0e-5f);
        }

        beginTest ("Swapping models while playing: smooth, and the old one is freed");
        {
            NamAmpBlock amp;
            amp.prepare (spec);
            amp.installModel (loadModel ("wavenet_a1_standard.nam"));
            runMono (amp, sine (110.0, 0.3f, 9600));

            amp.installModel (loadModel ("lstm.nam"));
            const auto during = runMono (amp, sine (110.0, 0.3f, 9600));

            bool finite = true;
            for (auto x : during)
                finite = finite && std::isfinite (x);
            expect (finite);

            expectEquals (amp.getNumModelsInMemory(), 2, "old model kept until the message thread frees it");
            amp.collectGarbage();
            expectEquals (amp.getNumModelsInMemory(), 1);
            expect (amp.getLoadedFile().getFileName() == "lstm.nam");
        }

        beginTest ("Playing, sweeping and swapping amps never allocates on the audio thread");
        {
            Chain chain;
            auto& amp = dynamic_cast<NamAmpBlock&> (chain.addBlock (std::make_unique<NamAmpBlock>()));
            chain.addBlock (std::make_unique<NoiseGateBlock>());
            chain.prepare (spec);
            amp.installModel (loadModel ("wavenet_a1_standard.nam"));

            auto secondModel = loadModel ("lstm.nam");
            auto signal = sine (110.0, 0.3f, blockSize);
            const auto before = realtimeAllocationCount();

            for (int i = 0; i < 1500; ++i)
            {
                amp.findParameter ("bass")->setValue (i % 2 == 0 ? 10.0f : -10.0f);
                amp.findParameter ("input")->setValue ((float) (i % 24));
                amp.findParameter ("normalise")->setValue ((float) ((i / 100) % 2));

                if (i == 700)
                    amp.installModel (std::move (secondModel));   // message-thread work

                const rt::ScopedRealtimeSection realtime;
                float* channels[1] = { signal.data() };
                chain.process (channels, 1, blockSize);
            }

            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

class KeyedGateTests final : public juce::UnitTest
{
public:
    KeyedGateTests() : juce::UnitTest ("Gate keyed from the guitar", "Encore") {}

    void runTest() override
    {
        const auto hiss = noise (juce::Decibels::decibelsToGain (-30.0f), 96000);   // loud amp hiss
        const std::vector<float> silentGuitar (96000, 0.0f);

        auto run = [&] (float detectGuitar)
        {
            NoiseGateBlock gate;
            gate.prepare (spec);
            gate.findParameter ("threshold")->setValue (-50.0f);
            gate.findParameter ("detectGuitar")->setValue (detectGuitar);

            auto signal = hiss;

            for (int offset = 0; offset < (int) signal.size(); offset += blockSize)
            {
                float* ch[1] = { signal.data() + offset };
                gate.process (ch, 1, blockSize, ProcessContext { silentGuitar.data() + offset });
            }

            return signal;
        };

        beginTest ("Detect guitar: hiss louder than the threshold is still cut when the guitar is silent");
        expectLessThan (toDb (rms (run (1.0f), 72000, 96000)), -100.0f);

        beginTest ("Detect here: the same hiss keeps the gate open");
        expectWithinAbsoluteError (toDb (rms (run (0.0f), 72000, 96000)), -30.0f - 4.8f, 1.0f);
    }
};

static NamModelTests namModelTests;
static NamAmpBlockTests namAmpBlockTests;
static KeyedGateTests keyedGateTests;

} // namespace encore::test
