// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_dsp/juce_dsp.h>

#include "engine/core/BackgroundJobs.h"
#include "engine/core/Block.h"
#include "engine/core/RealtimeHandover.h"
#include "engine/dsp/Biquad.h"

namespace encore
{

/** The cabinet: a speaker impulse response (.wav/.aiff) convolved with the amp.

    Most NAM captures are amp-only, so this is what turns them into a guitar
    tone. Zero latency (JUCE's uniform-partitioned convolution), low/high cut
    to tame boom and fizz, a level control and a dry/wet mix.

    Loading: the file is read, mixed to mono and capped in length on a
    background thread. The audio buffer travels to the audio thread through a
    RealtimeHandover and is given to the convolution engine there (its load
    call is wait-free and doesn't allocate). The engine resamples it to the
    device rate in its own background thread and crossfades to it over 50 ms,
    so changing cabs while playing never leaves a gap.
*/
class CabIrBlock final : public Block
{
public:
    static constexpr auto typeId = "cab";
    static constexpr double maxIrSeconds = 1.0;   // plenty for cabs; keeps CPU bounded

    CabIrBlock();
    ~CabIrBlock() override;

    /** A loaded IR on its way to the audio thread. */
    struct ImpulseResponse
    {
        juce::AudioBuffer<float> samples;   // mono
        double sampleRate = 0.0;
        juce::File file;
        double lengthSeconds = 0.0;
        bool wasShortened = false;
    };

    /** Reads an IR file into memory: mono mix-down, length cap. Any thread
        except the audio thread. Returns null and sets error on failure. */
    static std::unique_ptr<ImpulseResponse> readFile (const juce::File& file, juce::String& error);

    //== Block =================================================================
    const FileSlot* getFileSlot() const noexcept override { return &fileSlot; }
    void loadFileAsync (const juce::File& file, std::function<void (juce::String)> onDone) override;
    juce::File getLoadedFile() const override { return loadedFile; }
    juce::String getLoadedFileInfo() const override { return loadedInfo; }
    void collectGarbage() override { irs.collectGarbage(); }
    int getLatencySamples() const noexcept override { return 0; }

    //== Message thread ========================================================
    /** Hands an IR to the audio thread. Used by loadFileAsync() and tests. */
    void installImpulseResponse (std::unique_ptr<ImpulseResponse> ir);

    /** For a block that isn't playing yet (a chain being built): loads the IR
        straight into the engine, so it's fully ready after prepare(). */
    void loadBeforePlaying (std::unique_ptr<ImpulseResponse> ir);

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;
    void updateFilters (int numSamples) noexcept;
    void describe (const ImpulseResponse& ir);

    const FileSlot fileSlot { "Load cab IR...", "*.wav;*.aif;*.aiff;*.flac" };

    Parameter& lowCut;
    Parameter& highCut;
    Parameter& level;
    Parameter& mix;

    /** One background thread shared by every cab block (patches preload many). */
    struct SharedQueue { juce::dsp::ConvolutionMessageQueue queue; };
    juce::SharedResourcePointer<SharedQueue> sharedQueue;
    juce::dsp::Convolution convolution;

    RealtimeHandover<ImpulseResponse, 8> irs;
    juce::SharedResourcePointer<BackgroundJobs> jobs;

    // Message-thread copy of what's loaded, for the UI.
    juce::File loadedFile;
    juce::String loadedInfo { "No cab loaded: amp passes through" };

    // Audio-thread state.
    bool hasImpulseResponse = false;
    double sampleRate = 48000.0;
    std::vector<float> dry;
    std::array<dsp::Biquad<1>, 2> cuts;
    std::array<bool, 2> cutActive {};
    bool filtersValid = false;
};

} // namespace encore
