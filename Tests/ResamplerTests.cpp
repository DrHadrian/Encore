// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "TestSignals.h"

#include "engine/dsp/StreamResampler.h"

namespace encore::test
{

class ResamplerTests final : public juce::UnitTest
{
public:
    ResamplerTests() : juce::UnitTest ("Sample-rate bridge", "Encore") {}

    static std::vector<float> sineAt (double rate, double frequency, float amplitude, int numSamples)
    {
        std::vector<float> s ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
            s[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / rate);

        return s;
    }

    /** Passes a signal through a bridge with an identity inner processor, in varying block sizes. */
    static std::vector<float> throughBridge (dsp::RateBridge& bridge, std::vector<float> signal,
                                             const std::vector<int>& blockSizes)
    {
        size_t which = 0;

        for (int offset = 0; offset < (int) signal.size();)
        {
            const auto n = juce::jmin (blockSizes[which++ % blockSizes.size()], (int) signal.size() - offset);
            bridge.process (signal.data() + offset, n, [] (float*, int) {});
            offset += n;
        }

        return signal;
    }

    void runTest() override
    {
        beginTest ("44.1 kHz device, 48 kHz model: level kept, no underruns, any block size");
        {
            dsp::RateBridge bridge;
            bridge.prepare (44100.0, 48000.0, 512);

            const auto input = sineAt (44100.0, 1000.0, 0.5f, 88200);
            const auto output = throughBridge (bridge, input, { 128, 64, 37, 512, 1, 200 });

            expectEquals (bridge.getUnderrunCount(), 0);
            expectWithinAbsoluteError (toDb (rms (output, 44100, 88200) / rms (input, 44100, 88200)), 0.0f, 0.05f);
        }

        beginTest ("Reported latency matches the measured delay");
        for (auto rates : { std::pair { 44100.0, 48000.0 }, std::pair { 96000.0, 48000.0 }, std::pair { 48000.0, 44100.0 } })
        {
            dsp::RateBridge bridge;
            bridge.prepare (rates.first, rates.second, 128);

            std::vector<float> impulse (4096, 0.0f);
            impulse[0] = 1.0f;
            const auto output = throughBridge (bridge, impulse, { 128 });

            const auto peakAt = (int) std::distance (output.begin(),
                                                     std::max_element (output.begin(), output.end(),
                                                                       [] (float a, float b) { return std::abs (a) < std::abs (b); }));
            expectWithinAbsoluteError (peakAt, bridge.getLatencySamples(), 2);
            expectLessThan (bridge.getLatencySamples(), (int) (0.001 * rates.first) + 1, "under 1 ms");
        }

        beginTest ("Going down in rate, tones above the model's Nyquist are removed (no aliasing)");
        {
            dsp::RateBridge bridge;
            bridge.prepare (96000.0, 48000.0, 128);

            // 30 kHz can't exist at 48 kHz; it must not fold back into the audible band.
            const auto input = sineAt (96000.0, 30000.0, 0.5f, 96000);
            const auto output = throughBridge (bridge, input, { 128 });
            expectLessThan (toDb (rms (output, 48000, 96000) / rms (input, 48000, 96000)), -60.0f);
        }

        beginTest ("Audible content passes flat across the guitar range");
        for (auto frequency : { 80.0, 1000.0, 8000.0, 16000.0 })
        {
            dsp::RateBridge bridge;
            bridge.prepare (44100.0, 48000.0, 128);
            const auto input = sineAt (44100.0, frequency, 0.5f, 44100);
            const auto output = throughBridge (bridge, input, { 128 });
            expectWithinAbsoluteError (toDb (rms (output, 22050, 44100) / rms (input, 22050, 44100)), 0.0f, 0.1f);
        }

        beginTest ("The inner processor really runs at the inner rate");
        {
            dsp::RateBridge bridge;
            bridge.prepare (44100.0, 48000.0, 128);

            std::int64_t innerSamples = 0;
            std::vector<float> signal (44100, 0.0f);

            for (int offset = 0; offset < 44100; offset += 128)
                bridge.process (signal.data() + offset, juce::jmin (128, 44100 - offset),
                                [&] (float*, int n) { innerSamples += n; });

            expectWithinAbsoluteError ((double) innerSamples, 48000.0, 40.0);
        }
    }
};

static ResamplerTests resamplerTests;

} // namespace encore::test
