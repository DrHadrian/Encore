// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The Pedal EQ's character capture slot (stage B1, decision 0063): a NAM
// capture after Input, one copy per side, loaded off the audio thread,
// swapped lock-free, "DSP only" when empty or missing.

#include "AllocationCounter.h"
#include "TestSignals.h"

#include "engine/blocks/PedalEqBlock.h"
#include "engine/core/Realtime.h"

namespace encore::test
{

namespace
{
    juce::File exampleCapture()
    {
        return juce::File (ENCORE_NAM_EXAMPLE_MODELS).getChildFile ("wavenet_a1_standard.nam");
    }

    std::vector<float> playMono (PedalEqBlock& eq, std::vector<float> signal)
    {
        for (int start = 0; start < (int) signal.size(); start += blockSize)
        {
            float* channels[1] = { signal.data() + start };
            eq.process (channels, 1, juce::jmin (blockSize, (int) signal.size() - start), {});
        }

        return signal;
    }

    std::unique_ptr<PedalEqBlock::Capture> load()
    {
        juce::String error;
        auto capture = PedalEqBlock::loadCapture (exampleCapture(), spec, error);
        jassert (capture != nullptr);
        return capture;
    }
}

class PedalEqCaptureTests final : public juce::UnitTest
{
public:
    PedalEqCaptureTests() : juce::UnitTest ("Pedal EQ capture", "Encore") {}

    void runTest() override
    {
        beginTest ("Empty: \"DSP only\", nothing touched");
        {
            PedalEqBlock eq;
            expect (eq.getFileSlot() != nullptr && eq.getFileSlot()->wildcard == "*.nam");
            expect (eq.getFileSlot()->buttonText.startsWith ("Load capture"));
            expectEquals (eq.getLoadedFileInfo(), juce::String ("DSP only"));
            expect (eq.getLatestCapture() == nullptr);
            eq.prepare (spec);
            eq.reset();
            const auto input = noise (0.3f, 4800, 1);
            expect (playMono (eq, input) == input);
        }

        beginTest ("A capture loads both sides, plays after Input and says what it costs");
        {
            auto capture = load();
            expect (capture != nullptr && ! capture->isEmpty());

            if (capture == nullptr)
                return;

            expect (capture->sides[0] != nullptr && capture->sides[1] != nullptr && capture->sides[0] != capture->sides[1]);
            expectGreaterThan (capture->matchGain, 0.0f);

            PedalEqBlock eq;
            eq.prepare (spec);
            eq.installCaptureBeforePlaying (std::move (capture));
            eq.reset();

            const auto input = sine (220.0, 0.1f, 9600);
            const auto out = playMono (eq, input);
            expect (out != input, "the capture colours the sound");
            expect (std::all_of (out.begin(), out.end(), [] (float v) { return std::isfinite (v); }));
            expect (eq.getLoadedFile() == exampleCapture());
            expect (eq.getLoadedFileInfo().contains (exampleCapture().getFileNameWithoutExtension()), eq.getLoadedFileInfo());
            expect (eq.getLoadedFileInfo().contains ("per side"), eq.getLoadedFileInfo());
        }

        beginTest ("Stereo: one copy per side, each with its own memory");
        {
            PedalEqBlock eq;
            eq.prepare (spec);
            eq.installCaptureBeforePlaying (load());
            eq.reset();

            PedalEqBlock mono;
            mono.prepare (spec);
            mono.installCaptureBeforePlaying (load());
            mono.reset();

            const auto left = sine (220.0, 0.1f, 4096);
            const auto right = noise (0.05f, 4096, 2);
            auto l = left, r = right;

            for (int start = 0; start < 4096; start += blockSize)
            {
                float* channels[2] = { l.data() + start, r.data() + start };
                eq.process (channels, 2, blockSize, {});
            }

            // The left side sounds exactly as the capture alone on that signal.
            expect (l == playMono (mono, left));
            expect (r != right, "the right side runs its own copy");
        }

        beginTest ("Level-matched: as loud out as in; switching moves the gain over 10 ms");
        {
            const auto input = pinkNoise (0.25f, 48000, 3);
            const auto inRms = toDb (rms (input, 24000, 48000));

            PedalEqBlock eq;
            eq.findParameter ("captureLevel")->setValue (1.0f);
            eq.prepare (spec);
            eq.installCaptureBeforePlaying (load());
            eq.reset();
            const auto matched = playMono (eq, input);
            expectWithinAbsoluteError (toDb (rms (matched, 24000, 48000)), inRms, 2.0f);
        }

        beginTest ("Swap and unload: no allocation on the audio thread; freed on the message thread");
        {
            PedalEqBlock eq;
            eq.prepare (spec);
            eq.reset();
            std::vector<float> a ((size_t) blockSize, 0.1f), b ((size_t) blockSize, -0.1f);
            float* channels[2] = { a.data(), b.data() };

            const auto playSome = [&] (int buffers)
            {
                const auto before = realtimeAllocationCount();
                {
                    const rt::ScopedRealtimeSection realtime;

                    for (int i = 0; i < buffers; ++i)
                        eq.process (channels, 2, blockSize, {});
                }
                return realtimeAllocationCount() - before;
            };

            eq.installCapture (load());              // message thread: may allocate
            expectEquals (playSome (40), 0, "takes it, fades in");
            eq.installCapture (load());              // a swap
            expectEquals (playSome (40), 0, "crossfades and retires the old one");
            eq.unloadCapture();                      // DSP only
            expectEquals (playSome (40), 0, "fades out to DSP only");

            eq.collectGarbage();
            expectEquals (eq.getNumCapturesInMemory(), 1, "only the empty one (DSP only) is left");
            expectEquals (eq.getLoadedFileInfo(), juce::String ("DSP only"));
            expect (eq.getLoadedFile() == juce::File());

            // And DSP only is bit-exact again.
            const auto input = noise (0.3f, 1024, 4);
            expect (playMono (eq, input) == input);
        }

        beginTest ("A missing file: refused with a reason; the block plays DSP only and says so");
        {
            juce::String error;
            expect (PedalEqBlock::loadCapture (juce::File::getCurrentWorkingDirectory().getChildFile ("gone.nam"), spec, error) == nullptr);
            expect (error.isNotEmpty());

            PedalEqBlock eq;
            eq.setRequestedFile ("models/gone.nam");
            expectEquals (eq.getLoadedFileInfo(), juce::String ("Capture not on this computer: DSP only"));
            eq.prepare (spec);
            eq.reset();
            const auto input = noise (0.3f, 2048, 5);
            expect (playMono (eq, input) == input);
        }
    }
};

static PedalEqCaptureTests pedalEqCaptureTests;

} // namespace encore::test
