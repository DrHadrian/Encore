// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "engine/core/Block.h"
#include "engine/core/BackgroundJobs.h"
#include "engine/core/RealtimeHandover.h"
#include "engine/dsp/Biquad.h"
#include "engine/nam/NamModel.h"

namespace encore
{

/** The amp: a Neural Amp Modeler capture with input/output gain, optional
    loudness matching, and a simple bass/mid/treble tone stack after it.

    Loading a model happens on a background thread. The finished model comes
    in through a RealtimeHandover, and the audio thread crossfades from the
    old model to the new one over 30 ms, so changing amps while playing never
    leaves a gap. The old model is freed later on the message thread.

    Without a model the block passes the guitar through (gains and tone still apply).
*/
class NamAmpBlock final : public Block
{
public:
    static constexpr auto typeId = "nam";
    static constexpr float normalisedLoudnessDb = -18.0f;   // NAM's usual target

    NamAmpBlock();
    ~NamAmpBlock() override;

    //== Block =================================================================
    const FileSlot* getFileSlot() const noexcept override { return &fileSlot; }
    void loadFileAsync (const juce::File& file, std::function<void (juce::String)> onDone) override;
    juce::File getLoadedFile() const override;
    juce::String getLoadedFileInfo() const override;
    void collectGarbage() override { models.collectGarbage(); }
    int getLatencySamples() const noexcept override;
    int getOutputChannels (int) const noexcept override { return 1; }   // an amp is mono

    //== Message thread ========================================================
    /** Hands a loaded model to the audio thread (prepares it first if needed).
        Used by loadFileAsync(), and directly by tests and the show loader. */
    void installModel (std::unique_ptr<NamModel> model);

    /** For a block that isn't playing yet (a chain being built): the model is
        active from the first sample, with no fade-in from the dry guitar. */
    void installBeforePlaying (std::unique_ptr<NamModel> model);

    /** The model the UI should describe (newest installed), or null. */
    const NamModel* getLatestModel() const noexcept { return models.getLatest(); }

    /** Models still in memory: the playing one plus any not yet freed. */
    int getNumModelsInMemory() const { return models.getNumOwned(); }

private:
    void prepareBlock (const ProcessSpec&) override;
    void processBlock (float* const* channels, int numChannels, int numSamples,
                       const ProcessContext&) noexcept override;
    void resetBlock() noexcept override;

    void runModel (NamModel* model, float* samples, const float* normaliseAmount, int numSamples) noexcept;
    void updateToneStack (int numSamples) noexcept;

    const FileSlot fileSlot { "Load amp...", "*.nam" };

    Parameter& inputGain;
    Parameter& outputGain;
    Parameter& bass;
    Parameter& mid;
    Parameter& treble;
    Parameter& normalise;

    RealtimeHandover<NamModel, 8> models;

    // Audio-thread state.
    NamModel* current = nullptr;
    NamModel* fadingOut = nullptr;
    bool fading = false;
    int fadePosition = 0, fadeLength = 0;

    double sampleRate = 48000.0;
    std::vector<float> fadeBuffer, normaliseCurve;
    std::array<dsp::Biquad<1>, 3> toneStack;
    bool toneValid = false;

    juce::SharedResourcePointer<BackgroundJobs> jobs;
};

} // namespace encore
