// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The CPU benchmark (native modules, decision 0062): each block's and each
// shared DSP piece's average and worst buffer time at 48 kHz in buffers of
// 64 samples, with fixed settings. A report, not a pass/fail test: module
// stages compare it with their plan's CPU targets. Numbers are this machine's
// (CI runners vary; Debug builds are much slower than what players run).
//
// Written to the test log, and to the file named by ENCORE_BENCHMARK_REPORT
// if set (CI prints and uploads it).

#include "TestSignals.h"

#include "engine/dsp/Detector.h"
#include "engine/dsp/FilterBand.h"
#include "engine/dsp/Oversampler.h"
#include "session/OfflineRender.h"

namespace encore::test
{

class BenchmarkTests final : public juce::UnitTest
{
public:
    BenchmarkTests() : juce::UnitTest ("Module CPU benchmark", "Encore") {}

    static constexpr double rate = 48000.0;
    static constexpr int buffer = 64;
    static constexpr double seconds = 5.0;

    // The Pedal EQ's plan target: every slider at +/-10 (so the clip is in the
    // path), Classic Q, Live quality; <= 0.5 % at 48 kHz / 64 (Proposed).
    static constexpr const char* pedalEqTargetCase =
        R"({ "g100": 10, "g200": -10, "g400": 10, "g800": -10, "g1k6": 10, "g3k2": -10, "g6k4": 10, "quality": "Live" })";
    static constexpr double pedalEqTargetPercent = 0.5;

    struct Line
    {
        juce::String name;
        double averageMicros, worstMicros;
    };

    /** Times `work` (one buffer) over `seconds` of audio. */
    template <typename Work>
    static Line time (const juce::String& name, Work&& work)
    {
        const auto buffers = (int) (seconds * rate / buffer);
        double total = 0.0, worst = 0.0;

        for (int b = 0; b < buffers; ++b)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            work (b);
            const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            total += elapsed;
            worst = juce::jmax (worst, elapsed);
        }

        return { name, total / buffers * 1.0e6, worst * 1.0e6 };
    }

    static juce::String format (const Line& line)
    {
        const auto bufferMicros = buffer / rate * 1.0e6;
        return line.name.paddedRight (' ', 46) + juce::String (line.averageMicros, 2).paddedLeft (' ', 8) + " us"
               + juce::String (line.averageMicros / bufferMicros * 100.0, 2).paddedLeft (' ', 8) + " %"
               + juce::String (line.worstMicros, 1).paddedLeft (' ', 9) + " us"
               + juce::String (line.worstMicros / bufferMicros * 100.0, 1).paddedLeft (' ', 7) + " %";
    }

    void runTest() override
    {
        beginTest ("CPU per buffer at 48 kHz / 64 samples (a report)");
        const juce::ScopedNoDenormals noDenormals;   // as in the audio callback (AudioEngine)

        std::vector<Line> lines;
        const auto input = pinkNoise (0.3f, (int) (seconds * rate), 21);

        //== Whole blocks, through the render core (as the chain runs them) ====
        juce::AudioBuffer<float> mono (1, (int) input.size());
        mono.copyFrom (0, 0, input.data(), (int) input.size());
        juce::AudioBuffer<float> stereo (2, (int) input.size());
        stereo.copyFrom (0, 0, input.data(), (int) input.size());
        stereo.copyFrom (1, 0, input.data(), (int) input.size());

        struct Case { const char* label; const char* type; const char* settings; bool stereo; };
        const Case cases[] = {
            { "engineProbe: null (bit-exact)",              "engineProbe", "{}", false },
            { "engineProbe: bell",                          "engineProbe", R"({ "shape": "Bell", "gain": 6, "freq": 800, "q": 1 })", false },
            { "engineProbe: bell, stereo",                  "engineProbe", R"({ "shape": "Bell", "gain": 6, "freq": 800, "q": 1 })", true },
            { "engineProbe: low cut 96 dB/oct",             "engineProbe", R"({ "shape": "Low cut", "slope": "96 dB/oct", "freq": 90 })", false },
            { "engineProbe: drive, Live (2x)",              "engineProbe", R"({ "drive": 12, "quality": "Live" })", false },
            { "engineProbe: drive, High (4x)",              "engineProbe", R"({ "drive": 12, "quality": "High" })", false },
            { "engineProbe: drive, Max (8x)",               "engineProbe", R"({ "drive": 12, "quality": "Max" })", false },
            { "pedalEq: flat (bit-exact)",                  "pedalEq",     "{}", false },
            { "pedalEq: +/-10, Classic, Live (clip in)",    "pedalEq",     pedalEqTargetCase, false },
            { "pedalEq: +/-10, Classic, High",              "pedalEq",     R"({ "g100": 10, "g200": -10, "g400": 10, "g800": -10, "g1k6": 10, "g3k2": -10, "g6k4": 10, "quality": "High" })", false },
            { "pedalEq: +/-10, Classic, Max",               "pedalEq",     R"({ "g100": 10, "g200": -10, "g400": 10, "g800": -10, "g1k6": 10, "g3k2": -10, "g6k4": 10, "quality": "Max" })", false },
            { "pedalEq: +/-10, Live, stereo",               "pedalEq",     pedalEqTargetCase, true },
            { "eq (today's EQ, for comparison)",            "eq",          R"({ "bass": 3, "mid": -3, "treble": 2 })", false },
            { "gate (today's gate, for comparison)",        "gate",        "{}", false },
            { "delay (today's delay, for comparison)",      "delay",       "{}", false },
        };

        juce::String targets;

        for (const auto& c : cases)
        {
            const auto r = render::renderBlock (c.stereo ? stereo : mono, rate, c.type, juce::JSON::parse (c.settings), buffer);
            expect (r.ok, r.error);

            if (r.ok && ! r.cpu.empty())
            {
                lines.push_back ({ juce::String ("block ") + c.label, r.cpu[0].averageMicros, r.cpu[0].worstMicros });

                // The plan's target (EQ-PLAN, Proposed): the Pedal EQ <= 0.5 % at Live.
                if (c.settings == pedalEqTargetCase && ! c.stereo)
                    targets << "Target: pedalEq +/-10, Classic, Live <= " << juce::String (pedalEqTargetPercent, 1) << " % of the buffer: "
                            << juce::String (r.cpu[0].averagePercent, 2) << " % average, "
                            << (r.cpu[0].averagePercent <= pedalEqTargetPercent ? "within the target" : "OVER THE TARGET") << "\n";
            }
        }

        //== The shared DSP pieces on their own ================================
        std::vector<float> work ((size_t) buffer);
        const auto load = [&] (int b) { std::copy_n (input.begin() + ((b * buffer) % ((int) input.size() - buffer)), buffer, work.begin()); };

        {
            dsp::FilterBand<1> band;
            band.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::bell, 800.0, 6.0, 1.0 }, rate));
            lines.push_back (time ("dsp FilterBand: 1 section (bell)", [&] (int b) { load (b); float* ch[1] = { work.data() }; band.process (ch, 1, buffer); }));
        }
        {
            dsp::FilterBand<1> band;
            band.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, 90.0, 0.0, 0.7071, 96 }, rate));
            lines.push_back (time ("dsp FilterBand: 8 sections (96 dB/oct)", [&] (int b) { load (b); float* ch[1] = { work.data() }; band.process (ch, 1, buffer); }));
        }
        {
            dsp::FilterBand<1> band;
            auto f = 100.0;
            lines.push_back (time ("dsp FilterBand: bell, redesigned every 16", [&] (int b)
            {
                load (b);
                for (int start = 0; start < buffer; start += 16)
                {
                    f = f > 8000.0 ? 100.0 : f * 1.01;
                    band.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::bell, f, 6.0, 1.0 }, rate));
                    float* ch[1] = { work.data() + start };
                    band.process (ch, 1, 16);
                }
            }));
        }
        {
            dsp::LevelDetector detector;
            dsp::GateEnvelope gate;
            detector.prepare (rate);
            gate.prepare (rate);
            lines.push_back (time ("dsp LevelDetector + GateEnvelope (peak)", [&] (int b)
            {
                load (b);
                for (auto& x : work)
                    x *= gate.process (detector.process (x));
            }));
        }
        for (int q = 0; q < dsp::Oversampler::numQualities; ++q)
        {
            dsp::Oversampler os;
            os.setQuality ((dsp::Quality) q);
            os.prepare (1, buffer, rate);
            const auto tanhStage = [] (float* const* ch, int n, int len, double, int)
            {
                for (int c = 0; c < n; ++c)
                    for (int i = 0; i < len; ++i)
                        ch[c][i] = std::tanh (4.0f * ch[c][i]) * 0.25f;
            };
            lines.push_back (time ("dsp Oversampler + tanh, " + juce::String (dsp::Oversampler::factorFor ((dsp::Quality) q)) + "x",
                                   [&] (int b) { load (b); float* ch[1] = { work.data() }; os.process (ch, 1, buffer, tanhStage); }));
        }

        //== The report ========================================================
        juce::String report;
        report << "Encore module CPU benchmark: 48 kHz, buffers of 64 samples (1333 us), " << seconds << " s of pink noise, mono unless said\n";
        report << "Worst cases include the operating system's interruptions (on a shared CI machine they can be large)\n";
       #if JUCE_DEBUG
        report << "DEBUG BUILD: numbers are far above what players run\n";
       #endif
        report << juce::String ("what").paddedRight (' ', 46) << "   average          worst\n";

        for (const auto& line : lines)
            report << format (line) << "\n";

        report << targets;

        logMessage (report);

        if (const auto path = juce::SystemStats::getEnvironmentVariable ("ENCORE_BENCHMARK_REPORT", {}); path.isNotEmpty())
            juce::File (path).replaceWithText (report);
    }
};

static BenchmarkTests benchmarkTests;

} // namespace encore::test
