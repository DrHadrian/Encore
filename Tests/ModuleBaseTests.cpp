// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B0 (the shared base, decision 0062): what every
// module needs from the engine and the show format.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/core/Parameter.h"
#include "engine/blocks/EngineProbeBlock.h"
#include "engine/core/Realtime.h"
#include "engine/dsp/Crossfade.h"
#include "model/ShowJson.h"

namespace encore::test
{

class ParameterChoiceTests final : public juce::UnitTest
{
public:
    ParameterChoiceTests() : juce::UnitTest ("Parameter choices", "Encore") {}

    void runTest() override
    {
        const juce::StringArray shapes { "Bell", "Low shelf", "High shelf", "Low cut" };

        beginTest ("A choice is a whole-step index, 0..n-1, shown by name");
        {
            Parameter p (Parameter::Spec::choice ("shape", "Shape", shapes, 1));
            expect (p.isChoice());
            expectEquals (p.getNumChoices(), 4);
            expectEquals (p.getValue(), 1.0f);
            expectEquals (p.getChoiceIndex(), 1);
            expectEquals (p.getSpec().range.start, 0.0f);
            expectEquals (p.getSpec().range.end, 3.0f);
            expectEquals (p.toText (0.0f), juce::String ("Bell"));
            expectEquals (p.toText (3.0f), juce::String ("Low cut"));
            expect (p.getSpec().unit.isEmpty());
        }

        beginTest ("A choice made with plain aggregate fields is sanitised the same way");
        {
            Parameter::Spec modeSpec { "mode", "Mode", { -10.0f, 10.0f }, 7.0f, "dB", 2, 0.05 };
            modeSpec.choices = { "Gate", "Expander", "Ducker" };
            Parameter p (modeSpec);
            expectEquals (p.getSpec().range.end, 2.0f);
            expectEquals (p.getValue(), 2.0f, "the default is clamped into range");
            expectEquals (p.getSpec().rampSeconds, 0.0);
        }

        beginTest ("Out-of-range and in-between values clamp and snap to a step");
        {
            Parameter p (Parameter::Spec::choice ("shape", "Shape", shapes));
            p.setValue (-3.0f);
            expectEquals (p.getChoiceIndex(), 0);
            p.setValue (99.0f);
            expectEquals (p.getChoiceIndex(), 3);
            p.setValue (1.4f);
            expectEquals (p.getValue(), 1.0f);
            p.setValue (1.6f);
            expectEquals (p.getValue(), 2.0f);
            expectEquals (p.toText (42.0f), juce::String ("Low cut"), "text of an out-of-range value names the nearest step");
        }

        beginTest ("A choice never ramps: the next sample already has the new step");
        {
            Parameter p (Parameter::Spec::choice ("shape", "Shape", shapes));
            p.prepare (sampleRate);
            p.setValue (2.0f);
            p.beginBlock();
            expect (! p.isSmoothing());
            expectEquals (p.nextValue(), 2.0f);

            // A continuous parameter does ramp, for comparison.
            Parameter gain ({ "gain", "Gain", { -12.0f, 12.0f }, 0.0f, "dB" });
            gain.prepare (sampleRate);
            gain.setValue (12.0f);
            gain.beginBlock();
            expect (gain.isSmoothing());
        }

        beginTest ("Saved by index: a show round trip keeps the step exactly");
        {
            auto show = makeDefaultShow ("Choices");
            auto& block = show.patches[0].mainLane().blocks[0];
            block.params["shape"] = 3.0f;

            juce::String error;
            const auto text = json::toJson (show);
            const auto loaded = json::fromJson (text, error);
            expect (loaded.has_value(), error);

            if (loaded)
                expectEquals (loaded->patches[0].mainLane().blocks[0].params.at ("shape"), 3.0f);

            Parameter p (Parameter::Spec::choice ("shape", "Shape", shapes));
            p.setValue (loaded ? loaded->patches[0].mainLane().blocks[0].params.at ("shape") : 0.0f);
            expectEquals (p.toText (p.getValue()), juce::String ("Low cut"));
        }
    }
};

class CrossfadeTests final : public juce::UnitTest
{
public:
    CrossfadeTests() : juce::UnitTest ("Choice crossfade", "Encore") {}

    void runTest() override
    {
        beginTest ("Fades from the old path to the new over 10 ms, then passes the new path");
        {
            dsp::Crossfade fade;
            fade.prepare (2, blockSize, sampleRate);
            expect (! fade.isActive());
            expectEquals (fade.getLengthSamples(), 480);

            fade.start();
            expect (fade.isActive());

            std::vector<float> output;
            std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize);

            for (int b = 0; b < 6; ++b)
            {
                std::fill (left.begin(), left.end(), 0.0f);    // the old path's input
                std::fill (right.begin(), right.end(), 0.0f);
                const float* in[2] = { left.data(), right.data() };
                fade.copyInput (in, 2, blockSize);

                // Old setting: silence. New setting: 1.0.
                if (fade.isActive())
                    for (int ch = 0; ch < 2; ++ch)
                        juce::FloatVectorOperations::fill (fade.oldChannels()[ch], 0.0f, blockSize);

                std::fill (left.begin(), left.end(), 1.0f);
                std::fill (right.begin(), right.end(), 1.0f);
                float* out[2] = { left.data(), right.data() };

                if (fade.isActive())
                    fade.mix (out, 2, blockSize);

                output.insert (output.end(), left.begin(), left.end());
                expect (left == right, "every channel fades alike");
            }

            expect (! fade.isActive());
            expectLessThan (output.front(), 0.01f, "starts at the old path");
            expectEquals (output[479], 1.0f, "reaches the new path exactly at 10 ms");

            for (size_t i = 1; i < output.size(); ++i)
                expect (output[i] >= output[i - 1], "rises without a dip");

            for (size_t i = 480; i < output.size(); ++i)
                expectEquals (output[i], 1.0f);
        }

        beginTest ("No allocation on the audio thread");
        {
            dsp::Crossfade fade;
            fade.prepare (2, blockSize, sampleRate);
            std::vector<float> a ((size_t) blockSize, 0.5f), b ((size_t) blockSize, 0.25f);
            float* channels[2] = { a.data(), b.data() };

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 10; ++i)
                {
                    fade.start();
                    fade.copyInput (channels, 2, blockSize);
                    fade.mix (channels, 2, blockSize);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

class EngineProbeTests final : public juce::UnitTest
{
public:
    EngineProbeTests() : juce::UnitTest ("Engine test unit", "Encore") {}

    void runTest() override
    {
        beginTest ("Switching a shape crossfades: no jump in the output");
        {
            EngineProbeBlock probe;
            probe.prepare (spec);
            const auto input = sine (200.0f, 0.5f, 9600);
            auto maxInputStep = 0.0f;

            for (size_t i = 1; i < input.size(); ++i)
                maxInputStep = std::max (maxInputStep, std::abs (input[i] - input[i - 1]));

            // Off -> a 96 dB/oct low cut at 1 kHz, which removes the 200 Hz sine.
            std::vector<float> output;
            const auto switchAt = blockSize * (2400 / blockSize);

            for (int start = 0; start < (int) input.size(); start += blockSize)
            {
                if (start == switchAt)
                {
                    probe.findParameter ("freq")->setValue (1000.0f);
                    probe.findParameter ("slope")->setValue (15.0f);
                    probe.findParameter ("shape")->setValue (4.0f);   // Low cut
                }

                std::vector<float> chunk (input.begin() + start, input.begin() + start + blockSize);
                float* channels[1] = { chunk.data() };
                probe.process (channels, 1, blockSize, {});
                output.insert (output.end(), chunk.begin(), chunk.end());
            }

            auto maxStep = 0.0f;

            for (size_t i = 1; i < output.size(); ++i)
                maxStep = std::max (maxStep, std::abs (output[i] - output[i - 1]));

            expectLessThan (maxStep, maxInputStep * 1.5f, "a hard switch would jump");
            expectLessThan (peak (output, 7200, 9600), 0.01f, "the new setting is in place after the fade");
            expect (std::equal (output.begin(), output.begin() + switchAt, input.begin()), "Off is bit-exact");
        }

        beginTest ("No allocation while playing and switching");
        {
            EngineProbeBlock probe;
            probe.prepare (spec);
            std::vector<float> a ((size_t) blockSize, 0.25f), b ((size_t) blockSize, -0.25f);
            float* channels[2] = { a.data(), b.data() };
            auto* shapeParameter = probe.findParameter ("shape");   // looking up by name builds a String: not on the audio thread
            auto* freqParameter = probe.findParameter ("freq");

            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int i = 0; i < 200; ++i)
                {
                    shapeParameter->setValue ((float) (i / 10 % 9));
                    freqParameter->setValue (100.0f + (float) i * 50.0f);
                    probe.process (channels, 2, blockSize, {});
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static ParameterChoiceTests parameterChoiceTests;
static EngineProbeTests engineProbeTests;
static CrossfadeTests crossfadeTests;

} // namespace encore::test
