// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/blocks/CabIrBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

class CabIrTests final : public juce::UnitTest
{
public:
    CabIrTests() : juce::UnitTest ("Cab IR block", "Encore") {}

    /** Writes a WAV with an impulse at `delay` samples (per channel), returns the file. */
    static void writeImpulseWav (const juce::File& file, double rate, int numChannels, int length, int delay)
    {
        juce::AudioBuffer<float> b (numChannels, length);
        b.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            b.setSample (ch, delay, 1.0f);

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                             .withNumChannels (numChannels)
                                                             .withBitsPerSample (24);

        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (b, 0, length);
    }

    static std::unique_ptr<CabIrBlock::ImpulseResponse> impulseAt (int delay, double rate = sampleRate)
    {
        auto ir = std::make_unique<CabIrBlock::ImpulseResponse>();
        ir->samples.setSize (1, 512);
        ir->samples.clear();
        ir->samples.setSample (0, delay, 1.0f);
        ir->sampleRate = rate;
        ir->lengthSeconds = 512.0 / rate;
        return ir;
    }

    /** Plays noise through the block in real-time-ish steps so JUCE's loader
        thread can finish, then returns input and output of the last block. */
    static std::pair<std::vector<float>, std::vector<float>> settleAndCapture (CabIrBlock& cab)
    {
        std::vector<float> in, out;

        for (int i = 0; i < 300; ++i)
        {
            in = noise (0.3f, blockSize, i + 1);
            out = runMono (cab, in);
            juce::Thread::sleep (1);
        }

        return { in, out };
    }

    void runTest() override
    {
        const juce::TemporaryFile tempA (".wav"), tempB (".wav"), tempC (".wav");

        beginTest ("Reading: stereo IRs become mono, long ones are capped at 1 s");
        {
            writeImpulseWav (tempA.getFile(), 44100.0, 2, 4410, 0);
            juce::String error;
            auto ir = CabIrBlock::readFile (tempA.getFile(), error);
            expect (ir != nullptr, error);

            if (ir != nullptr)
            {
                expectEquals (ir->samples.getNumChannels(), 1);
                expectEquals (ir->sampleRate, 44100.0);
                expectWithinAbsoluteError (ir->samples.getSample (0, 0), 1.0f, 1.0e-4f);
                expect (! ir->wasShortened);
            }

            writeImpulseWav (tempB.getFile(), 48000.0, 1, 96000, 0);
            auto longIr = CabIrBlock::readFile (tempB.getFile(), error);
            expect (longIr != nullptr && longIr->wasShortened && longIr->samples.getNumSamples() == 48000);

            tempC.getFile().replaceWithText ("not audio");
            expect (CabIrBlock::readFile (tempC.getFile(), error) == nullptr);
            expect (error.isNotEmpty());
        }

        beginTest ("Without an IR the amp passes through untouched");
        {
            CabIrBlock cab;
            cab.prepare (spec);
            const auto input = noise (0.3f, 4800);
            expect (runMono (cab, input) == input);
        }

        beginTest ("Zero latency: an IR with its impulse at sample 0 doesn't delay the signal");
        {
            CabIrBlock cab;
            cab.prepare (spec);
            cab.installImpulseResponse (impulseAt (0));
            const auto [in, out] = settleAndCapture (cab);

            // Normalised to NAM's convention: unit energy at -18 dB (x 0.125).
            float maxError = 0.0f;
            for (size_t i = 0; i < in.size(); ++i)
                maxError = juce::jmax (maxError, std::abs (out[i] - in[i] * 0.125f));

            expectLessThan (maxError, 1.0e-4f);
        }

        beginTest ("Swapping IRs while playing is gapless and frees the old one");
        {
            CabIrBlock cab;
            cab.prepare (spec);
            cab.installImpulseResponse (impulseAt (0));
            settleAndCapture (cab);

            cab.installImpulseResponse (impulseAt (5));
            float minRms = 1.0f;

            for (int i = 0; i < 300; ++i)
            {
                const auto out = runMono (cab, noise (0.3f, blockSize, i + 7));
                minRms = juce::jmin (minRms, rms (out, 0, blockSize));
                juce::Thread::sleep (1);
            }

            expectGreaterThan (minRms, 0.3f * 0.125f * 0.4f, "no dropout during the crossfade");
            cab.collectGarbage();
        }

        beginTest ("Playing and swapping IRs never allocates on the audio thread");
        {
            CabIrBlock cab;
            cab.prepare (spec);
            cab.installImpulseResponse (impulseAt (0));
            settleAndCapture (cab);

            auto signal = noise (0.3f, blockSize);
            const auto before = realtimeAllocationCount();

            for (int i = 0; i < 400; ++i)
            {
                if (i == 100 || i == 250)
                    cab.installImpulseResponse (impulseAt (i == 100 ? 3 : 9));   // message-thread work

                cab.findParameter ("lowCut")->setValue (i % 2 == 0 ? 100.0f : 20.0f);

                {
                    const rt::ScopedRealtimeSection realtime;
                    float* ch[1] = { signal.data() };
                    cab.process (ch, 1, blockSize);
                }

                juce::Thread::sleep (1);   // let the loader thread work, as in real time
            }

            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static CabIrTests cabIrTests;

} // namespace encore::test
