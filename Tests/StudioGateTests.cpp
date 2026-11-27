// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// Native modules, stage B2: the Studio Gate "X4" (GATE-PLAN §3).

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalGateBlock.h"
#include "engine/blocks/StudioGateBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    constexpr double allRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    struct Io
    {
        std::vector<std::vector<float>> channels;
        const std::vector<float>* guitar = nullptr;
    };

    /** Runs a block over the channels in place, in blockSize chunks. */
    void run (Block& block, Io& io, int chunk = blockSize)
    {
        const auto n = (int) io.channels[0].size();

        for (int offset = 0; offset < n; offset += chunk)
        {
            const auto len = juce::jmin (chunk, n - offset);
            float* ch[2] = { io.channels[0].data() + offset, io.channels.size() > 1 ? io.channels[1].data() + offset : nullptr };
            ProcessContext context;

            if (io.guitar != nullptr)
                context.chainInput = io.guitar->data() + offset;

            block.process (ch, (int) io.channels.size(), len, context);
        }
    }

    void set (Block& block, const char* id, float value) { block.findParameter (id)->setValue (value); }

    void start (Block& block, double rate = sampleRate)
    {
        block.prepare ({ rate, blockSize });
        block.reset();
    }
}

class StudioGateTests final : public juce::UnitTest
{
public:
    StudioGateTests() : juce::UnitTest ("Studio Gate", "Encore") {}

    void runTest() override
    {
        beginTest ("Parameters: ids, ranges and defaults (today's gate's values)");
        {
            StudioGateBlock x4;
            expectEquals (x4.getTypeId(), juce::String ("studioGate"));
            const char* ids[] = { "mode", "threshold", "smartOffset", "smartCurve", "range", "ratio", "attack", "hold", "release",
                                  "hysteresis", "detector", "detectorTime", "keyHp", "keyLp", "key", "stereoLink", "lookahead", "lookaheadTime" };
            expectEquals ((int) x4.getParameters().size(), (int) std::size (ids));

            for (auto* id : ids)
                expect (x4.findParameter (id) != nullptr, id);

            expectEquals (x4.findParameter ("mode")->getChoiceIndex(), 0);
            expectEquals (x4.findParameter ("threshold")->getValue(), -60.0f);
            expectEquals (x4.findParameter ("range")->getValue(), -90.0f);
            expectEquals (x4.findParameter ("attack")->getValue(), 0.5f);
            expectEquals (x4.findParameter ("hold")->getValue(), 20.0f);
            expectEquals (x4.findParameter ("release")->getValue(), 80.0f);
            expectEquals (x4.findParameter ("hysteresis")->getValue(), 6.0f);
            expectEquals (x4.findParameter ("detectorTime")->getValue(), 10.0f);
            expectEquals (x4.findParameter ("key")->getChoiceIndex(), (int) StudioGateBlock::Key::guitar);
            expectEquals (x4.findParameter ("smartOffset")->getValue(), 9.0f);
            expectEquals (x4.findParameter ("ratio")->toText (4.0f), juce::String ("1:4.0"));
            expectEquals (x4.findParameter ("keyHp")->toText (10.0f), juce::String ("Off"));
            expectEquals (x4.findParameter ("keyLp")->toText (25000.0f), juce::String ("Off"));
            expectEquals (x4.findParameter ("keyLp")->toText (5000.0f), juce::String ("5.0 kHz"));
            expectEquals (x4.getLatencySamples(), 0);
            expect (! x4.hasTail());
        }

        beginTest ("Gate: closes on the noise, opens on a pluck");
        {
            StudioGateBlock x4;
            set (x4, "key", (float) StudioGateBlock::Key::input);
            start (x4);
            auto note = pluck (48000 * 2, 48000, 0.5f, 2000.0, -70.0f);
            Io io { { note.signal } };
            run (x4, io);
            expect (rms (io.channels[0], 40000, 47000) < rms (note.signal, 40000, 47000) * 1.0e-3f, "closed on noise below -60");
            expect (rms (io.channels[0], 50000, 60000) > rms (note.signal, 50000, 60000) * 0.99f, "open on the note");
        }

        beginTest ("Expander: the static curve, capped by Range");
        {
            for (auto [ratio, levelDb] : { std::pair { 2.0f, -50.0f }, std::pair { 4.0f, -45.0f }, std::pair { 20.0f, -42.0f } })
            {
                StudioGateBlock x4;
                set (x4, "mode", (float) StudioGateBlock::Mode::expander);
                set (x4, "key", (float) StudioGateBlock::Key::input);
                set (x4, "threshold", -40.0f);
                set (x4, "ratio", ratio);
                set (x4, "range", -30.0f);
                start (x4);
                Io io { { std::vector<float> (48000, juce::Decibels::decibelsToGain (levelDb)) } };
                run (x4, io);
                const auto expected = juce::jmin ((-40.0f - levelDb) * (ratio - 1.0f), 30.0f);
                expectWithinAbsoluteError (levelDb - toDb (io.channels[0].back()), expected, 0.05f, "1:" + juce::String (ratio));
            }
        }

        beginTest ("Ducker: turns its own input down while the key is loud");
        {
            StudioGateBlock x4;
            set (x4, "mode", (float) StudioGateBlock::Mode::ducker);
            set (x4, "threshold", -40.0f);
            set (x4, "range", -12.0f);
            set (x4, "attack", 5.0f);
            set (x4, "release", 300.0f);
            start (x4);

            // Its own input: a steady pad. The key (the guitar): playing, then silent.
            auto keyTone = sine (440.0, 0.3f, 96000);
            std::fill (keyTone.begin() + 48000, keyTone.end(), 0.0f);
            Io io { { std::vector<float> (96000, 0.2f) }, &keyTone };
            run (x4, io);
            expectWithinAbsoluteError (toDb (io.channels[0][40000]) - toDb (0.2f), -12.0f, 0.05f, "ducked while the key plays");
            expectWithinAbsoluteError (io.channels[0].back(), 0.2f, 1.0e-5f, "back after the release");
        }

        beginTest ("An X4 Smart preset sounds like the S2 with the same preset (within 1e-6)");
        {
            for (auto* id : { "cleanHiss", "singleCoils", "highGain", "djentTight" })
            {
                StudioGateBlock x4;
                PedalGateBlock s2;
                presets::apply (x4, *presets::find (x4, id));
                presets::apply (s2, *presets::find (s2, id));
                start (x4);
                start (s2);

                // Palm-mutes and a ringing note over hiss, with a cleaner guitar key.
                auto a = palmMute (48000 * 3, 24000, 0.5f, -58.0f, 3);
                auto b = pluck (48000 * 3, 72000, 0.4f, 2000.0, -58.0f, 98.0, 6, 4);
                std::vector<float> signal (a.signal.size()), clean (a.signal.size());

                for (size_t i = 0; i < signal.size(); ++i)
                {
                    signal[i] = a.signal[i] + b.signal[i];
                    clean[i] = 0.5f * (a.envelope[i] + b.envelope[i]) * (float) std::sin ((double) i * 0.0144);
                }

                Io x { { signal, signal }, &clean };
                Io s { { signal, signal }, &clean };
                run (x4, x);
                run (s2, s);
                auto maxDifference = 0.0f;

                for (size_t i = 0; i < signal.size(); ++i)
                    maxDifference = juce::jmax (maxDifference, std::abs (x.channels[0][i] - s.channels[0][i]));

                expect (maxDifference <= 1.0e-6f, juce::String (id) + ": " + juce::String (maxDifference));
                expectWithinAbsoluteError (x4.getLearnedFloorDb(), s2.getLearnedFloorDb(), 1.0e-4f);
            }
        }

        beginTest ("The key filter's response: 12 dB/oct at the stated corners (key listen)");
        {
            StudioGateBlock x4;
            set (x4, "key", (float) StudioGateBlock::Key::input);
            set (x4, "keyHp", 400.0f);
            set (x4, "keyLp", 4000.0f);
            start (x4);
            x4.setKeyListen (true);

            auto heardDb = [&] (double hz)
            {
                Io io { { sine (hz, 0.5f, 24000) } };
                run (x4, io);
                return toDb (peak (io.channels[0], 12000, 24000) / 0.5f);
            };

            expectWithinAbsoluteError (heardDb (400.0), -3.01f, 0.2f, "-3 dB at the high-pass corner");
            expectWithinAbsoluteError (heardDb (200.0), -12.3f, 0.3f, "an octave under: -12.3 dB (2nd order)");
            expectWithinAbsoluteError (heardDb (4000.0), -3.01f, 0.3f, "-3 dB at the low-pass corner");
            expectWithinAbsoluteError (heardDb (1265.0), 0.0f, 0.2f, "flat in between");
            expect (heardDb (50.0) < -35.0f, "rumble out of the key");

            // Off at the ends of the knobs.
            StudioGateBlock open;
            set (open, "key", (float) StudioGateBlock::Key::input);
            start (open);
            open.setKeyListen (true);
            Io io { { sine (30.0, 0.5f, 48000) } };
            run (open, io);
            expectWithinAbsoluteError (toDb (peak (io.channels[0], 24000, 48000) / 0.5f), 0.0f, 0.05f, "both off: the key unfiltered");
        }

        beginTest ("Key listen crossfades in and out");
        {
            StudioGateBlock x4;
            set (x4, "key", (float) StudioGateBlock::Key::input);
            start (x4);
            auto tone = sine (220.0, 0.3f, 48000);
            Io io { { tone } };
            int offset = 0;

            for (; offset < 48000; offset += blockSize)
            {
                if (offset == 12800)
                    x4.setKeyListen (true);

                if (offset == 25600)
                    x4.setKeyListen (false);

                float* ch[1] = { io.channels[0].data() + offset };
                x4.process (ch, 1, juce::jmin (blockSize, 48000 - offset));
            }

            auto maxError = 0.0f;

            for (size_t i = 0; i < tone.size(); ++i)
                maxError = juce::jmax (maxError, std::abs (io.channels[0][i] - tone[i]));

            expect (maxError < 1.0e-5f, "an open gate and an unfiltered key: the same signal throughout, no bump");
            expect (! x4.isKeyListening());
        }

        beginTest ("Independent stereo gates each channel on its own; linked follows the louder");
        {
            auto loud = pluck (48000 * 2, 4800, 0.5f, 400.0, -90.0f);
            std::vector<float> quiet (loud.signal.size(), 0.0f);

            for (auto mode : { StudioGateBlock::Stereo::linked, StudioGateBlock::Stereo::independent })
            {
                StudioGateBlock x4;
                set (x4, "key", (float) StudioGateBlock::Key::input);
                set (x4, "stereoLink", (float) mode);
                start (x4);

                // Right: a steady -50 dB tone (above the threshold); left: a note that ends.
                auto right = sine (330.0, juce::Decibels::decibelsToGain (-50.0f), (int) loud.signal.size());
                Io io { { loud.signal, right } };
                run (x4, io);
                const auto end = (int) loud.signal.size();

                if (mode == StudioGateBlock::Stereo::independent)
                {
                    expect (rms (io.channels[0], end - 4800, end) < rms (loud.signal, end - 4800, end) * 1.0e-3f, "left closes on its own");
                    expect (rms (io.channels[1], end - 4800, end) > rms (right, end - 4800, end) * 0.99f, "right stays open");
                }
                else
                {
                    expect (rms (io.channels[0], end - 4800, end) > rms (loud.signal, end - 4800, end) * 0.99f, "linked: the louder side keeps both open");
                }
            }
        }

        beginTest ("Look-ahead: the latency at every time and rate; the gate opens before the attack arrives");
        {
            for (auto rate : allRates)
            {
                for (auto ms : { 0.5f, 1.0f, 2.5f, 5.0f })
                {
                    StudioGateBlock x4;
                    set (x4, "lookahead", 1.0f);
                    set (x4, "lookaheadTime", ms);
                    start (x4, rate);
                    expectEquals (x4.getLatencySamples(), juce::roundToInt (ms * 0.001 * rate),
                                  juce::String (ms) + " ms at " + juce::String (rate));
                }
            }

            // A hard onset after silence: without look-ahead the attack ramp
            // eats the first samples; with 2 ms it is already open.
            auto onsetPeak = [&] (bool on)
            {
                StudioGateBlock x4;
                set (x4, "key", (float) StudioGateBlock::Key::input);
                set (x4, "attack", 1.0f);
                set (x4, "lookahead", on ? 1.0f : 0.0f);
                set (x4, "lookaheadTime", 2.0f);
                start (x4);
                std::vector<float> signal (48000, 0.0f);

                for (int i = 24000; i < 48000; ++i)
                    signal[(size_t) i] = 0.5f * (float) std::sin ((i - 24000) * 0.3);

                Io io { { signal } };
                run (x4, io);
                const auto delay = x4.getLatencySamples();
                return peak (io.channels[0], 24000 + delay, 24000 + delay + 12) / peak (signal, 24000, 24012);
            };

            expect (onsetPeak (false) < 0.5f, "without: the first quarter millisecond is cut");
            expectWithinAbsoluteError (onsetPeak (true), 1.0f, 1.0e-3f, "with 2 ms: the attack passes whole");

            // Switching it on while playing: the latency report follows, the audio crossfades.
            StudioGateBlock x4;
            set (x4, "key", (float) StudioGateBlock::Key::input);
            start (x4);
            auto tone = sine (220.0, 0.3f, 48000);
            Io io { { tone } };
            float maxStep = 0.0f;

            for (int offset = 0; offset < 48000; offset += blockSize)
            {
                if (offset == 12800)
                    set (x4, "lookahead", 1.0f);

                float* ch[1] = { io.channels[0].data() + offset };
                x4.process (ch, 1, blockSize);
            }

            for (size_t i = 1; i < tone.size(); ++i)
                maxStep = juce::jmax (maxStep, std::abs (io.channels[0][i] - io.channels[0][i - 1]));

            expectEquals (x4.getLatencySamples(), 48);
            expect (maxStep < 0.0087f, "no jump switching look-ahead on: " + juce::String (maxStep));
        }

        beginTest ("Mode switches crossfade; mono and stereo; no NaN at extremes");
        {
            for (int channels : { 1, 2 })
            {
                StudioGateBlock x4;
                set (x4, "key", (float) StudioGateBlock::Key::input);
                set (x4, "threshold", -20.0f);
                start (x4);
                auto tone = sine (220.0, 0.3f, 48000);
                Io io { std::vector<std::vector<float>> ((size_t) channels, tone) };
                auto finite = true;

                for (int offset = 0, k = 0; offset < 48000; offset += blockSize, ++k)
                {
                    if (k % 20 == 0)
                        set (x4, "mode", (float) ((k / 20) % 4));

                    float* ch[2] = { io.channels[0].data() + offset, channels > 1 ? io.channels[1].data() + offset : nullptr };
                    x4.process (ch, channels, blockSize);
                }

                for (auto& c : io.channels)
                    for (auto x : c)
                        finite = finite && std::isfinite (x) && std::abs (x) <= 0.3f + 1.0e-4f;

                expect (finite, "bounded by the input, every mode");
            }
        }

        beginTest ("Factory presets: every one loads; the setup isn't part of them");
        {
            StudioGateBlock x4;
            expectEquals ((int) x4.getFactoryPresets().size(), 8);

            for (const auto& preset : x4.getFactoryPresets())
            {
                presets::apply (x4, preset);
                start (x4);
                Io io { { pluck (9600, 480, 0.5f).signal } };
                run (x4, io);
                expect (std::isfinite (io.channels[0].back()), preset.id);
            }

            const auto* duck = presets::find (x4, "duckDelay");
            presets::apply (x4, *duck);
            expectEquals (x4.findParameter ("mode")->getChoiceIndex(), (int) StudioGateBlock::Mode::ducker);
            expectEquals (x4.findParameter ("key")->getChoiceIndex(), (int) StudioGateBlock::Key::block);
            expectEquals (x4.findParameter ("range")->getValue(), -12.0f);
            expect (x4.getRefs().empty(), "a preset never sets a reference");

            expect (! x4.isPresetParameter (*x4.findParameter ("stereoLink")));
            expect (! x4.isPresetParameter (*x4.findParameter ("lookahead")));
            expect (! x4.isPresetParameter (*x4.findParameter ("lookaheadTime")));
            expect (x4.isPresetParameter (*x4.findParameter ("key")));

            // Not picked yet: it keys from its own input and says so.
            start (x4);
            Io io { { std::vector<float> (4800, 0.1f) } };
            run (x4, io);
            expect (x4.getKeyState() == GateBlock::KeyState::noBlockPicked);
        }

        beginTest ("No allocation while playing, switching modes, keys, stereo and look-ahead");
        {
            StudioGateBlock x4;
            start (x4);
            auto note = pluck (48000, 4800, 0.5f);
            std::vector<float> right (note.signal);
            Parameter* params[] = { x4.findParameter ("mode"), x4.findParameter ("key"), x4.findParameter ("stereoLink"),
                                    x4.findParameter ("lookahead"), x4.findParameter ("keyHp"), x4.findParameter ("detector") };
            const auto before = realtimeAllocationCount();
            {
                const rt::ScopedRealtimeSection realtime;

                for (int offset = 0, k = 0; offset + blockSize <= 48000; offset += blockSize, ++k)
                {
                    if (k % 7 == 0)
                        params[(k / 7) % 6]->setValue ((float) ((k / 7) % 4));

                    x4.setKeyListen (k % 50 > 40);
                    float* ch[2] = { note.signal.data() + offset, right.data() + offset };
                    ProcessContext context;
                    context.chainInput = right.data() + offset;
                    x4.process (ch, 2, blockSize, context);
                }
            }
            expectEquals (realtimeAllocationCount() - before, 0);
        }
    }
};

static StudioGateTests studioGateTests;

} // namespace encore::test
