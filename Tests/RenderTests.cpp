// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// EncoreRender's core (native modules, decision 0062): settings, rendering
// through a block, WAV files.

#include "TestSignals.h"

#include "engine/blocks/EngineProbeBlock.h"
#include "session/OfflineRender.h"

namespace encore::test
{

class RenderTests final : public juce::UnitTest
{
public:
    RenderTests() : juce::UnitTest ("Offline render", "Encore") {}

    static juce::AudioBuffer<float> toBuffer (const std::vector<float>& left, const std::vector<float>& right = {})
    {
        juce::AudioBuffer<float> b (right.empty() ? 1 : 2, (int) left.size());
        b.copyFrom (0, 0, left.data(), (int) left.size());

        if (! right.empty())
            b.copyFrom (1, 0, right.data(), (int) right.size());

        return b;
    }

    static bool equal (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (! std::equal (a.getReadPointer (ch), a.getReadPointer (ch) + a.getNumSamples(), b.getReadPointer (ch)))
                return false;

        return true;
    }

    static juce::var json (const juce::String& text) { return juce::JSON::parse (text); }

    void runTest() override
    {
        beginTest ("A null setting renders bit-exact: the output equals the input");
        {
            const auto input = toBuffer (noise (0.5f, 48000, 1), noise (0.5f, 48000, 2));

            for (int bufferSize : { 1, 64, 100, 4096 })
            {
                const auto r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId, {}, bufferSize);
                expect (r.ok, r.error);
                expect (equal (r.output, input), "buffer " + juce::String (bufferSize));
                expectEquals (r.latencySamples, 0);
            }

            // A bell at 0 dB, a drive at 0 dB: still nothing to do.
            const auto r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId,
                                                json (R"({ "shape": "Bell", "gain": 0, "freq": 300 })"));
            expect (r.ok && equal (r.output, input));
        }

        beginTest ("Settings: numbers, choices by name; the trim does what it says");
        {
            const auto input = toBuffer (sine (1000.0, 0.25f, 48000));
            const auto r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId, json (R"({ "trim": -6 })"));
            expect (r.ok, r.error);
            expectWithinAbsoluteError (toDb (r.peakOut) - toDb (r.peakIn), -6.0f, 0.01f);
            expectWithinAbsoluteError (toDb (r.rmsIn), toDb (0.25f) - 3.01f, 0.05f);

            EngineProbeBlock probe;
            juce::String error;
            expect (render::applySettings (probe, json (R"({ "shape": "low cut", "slope": "48 dB/oct", "freq": "250" })"), error), error);
            expectEquals (probe.findParameter ("shape")->getChoiceIndex(), 4);
            expectEquals (probe.findParameter ("slope")->getChoiceIndex(), 7);
            expectEquals (probe.findParameter ("freq")->getValue(), 250.0f);
        }

        beginTest ("Bad input is refused with a reason");
        {
            const auto input = toBuffer (noise (0.5f, 480));
            auto r = render::renderBlock (input, sampleRate, "noSuchBlock", {});
            expect (! r.ok && r.error.contains ("noSuchBlock") && r.error.contains (EngineProbeBlock::typeId), r.error);

            r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId, json (R"({ "volume": 3 })"));
            expect (! r.ok && r.error.contains ("volume") && r.error.contains ("trim"), r.error);

            r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId, json (R"({ "shape": "Wobble" })"));
            expect (! r.ok && r.error.contains ("Wobble") && r.error.contains ("Low cut"), r.error);

            r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId, json ("[1, 2]"));
            expect (! r.ok);
            expect (r.report().startsWith ("Render failed"));
        }

        beginTest ("The report has levels, latency and CPU");
        {
            const auto input = toBuffer (sine (440.0, 0.5f, 48000));
            const auto r = render::renderBlock (input, sampleRate, EngineProbeBlock::typeId,
                                                json (R"({ "drive": 12, "quality": "Max" })"));
            expect (r.ok, r.error);
            expectGreaterThan (r.latencySamples, 0);
            expectEquals ((int) r.cpu.size(), 1);
            expectGreaterThan (r.cpu[0].worstMicros, 0.0);

            const auto text = r.report();
            expect (text.contains ("Peak") && text.contains ("RMS") && text.contains ("Latency " + juce::String (r.latencySamples))
                    && text.contains ("engineProbe"), text);
        }

        beginTest ("WAV files: 32-bit float, bit-exact round trip; a sweep to render");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EncoreRenderTests");
            const auto file = dir.getChildFile ("roundtrip.wav");
            const auto sweep = render::makeSweep (96000.0, 0.5);
            expectEquals (sweep.getNumSamples(), 48000);
            expectWithinAbsoluteError (sweep.getMagnitude (0, 0, sweep.getNumSamples()), 0.5f, 0.001f);

            juce::String error;
            expect (render::writeWav (file, sweep, 96000.0, error), error);
            const auto back = render::readAudioFile (file, error);
            expect (back.has_value(), error);

            if (back)
            {
                expectEquals (back->sampleRate, 96000.0);
                expect (equal (back->buffer, sweep));
            }

            expect (! render::readAudioFile (dir.getChildFile ("missing.wav"), error).has_value());
            expect (error.contains ("missing.wav"));
            dir.deleteRecursively();
        }
    }
};

static RenderTests renderTests;

} // namespace encore::test
