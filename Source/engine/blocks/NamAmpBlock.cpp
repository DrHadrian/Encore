// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/NamAmpBlock.h"

#include <juce_events/juce_events.h>

namespace encore
{

namespace
{
    constexpr double modelFadeSeconds = 0.03;
    constexpr int toneUpdateInterval = 32;

    // A simple amp-style tone stack after the capture.
    constexpr double bassHz = 150.0;
    constexpr double midHz = 650.0, midQ = 0.7;
    constexpr double trebleHz = 2500.0;
}

NamAmpBlock::NamAmpBlock()
    : Block (typeId, "Amp"),
      inputGain  (addParameter ({ "input",     "Input",     { -24.0f, 24.0f }, 0.0f, "dB", 1 })),
      outputGain (addParameter ({ "output",    "Output",    { -40.0f, 12.0f }, 0.0f, "dB", 1 })),
      bass       (addParameter ({ "bass",      "Bass",      { -10.0f, 10.0f }, 0.0f, "dB", 1 })),
      mid        (addParameter ({ "mid",       "Mid",       { -10.0f, 10.0f }, 0.0f, "dB", 1 })),
      treble     (addParameter ({ "treble",    "Treble",    { -10.0f, 10.0f }, 0.0f, "dB", 1 })),
      normalise  (addParameter ({ "normalise", "Level match", { 0.0f, 1.0f, 1.0f }, 1.0f, {}, 0 }))
{
}

NamAmpBlock::~NamAmpBlock() = default;

//==============================================================================
void NamAmpBlock::loadFileAsync (const juce::File& file, std::function<void (juce::String)> onDone)
{
    const auto spec = getPreparedSpec();
    juce::WeakReference<Block> weakThis (this);

    // Parsing, building, prewarming and benchmarking take from milliseconds to
    // a second: never on the message thread, never on the audio thread.
    jobs->add ([file, spec, weakThis, onDone]
    {
        juce::String error;
        auto model = NamModel::load (file, spec, error);
        auto holder = std::make_shared<std::unique_ptr<NamModel>> (std::move (model));

        juce::MessageManager::callAsync ([weakThis, holder, error, onDone]
        {
            if (auto* block = dynamic_cast<NamAmpBlock*> (weakThis.get()))
                if (*holder != nullptr)
                    block->installModel (std::move (*holder));

            if (onDone != nullptr)
                onDone (error);
        });
    });
}

void NamAmpBlock::installModel (std::unique_ptr<NamModel> model)
{
    jassert (model != nullptr);

    // The device may have changed rate while the model was loading.
    const auto& spec = getPreparedSpec();

    if (spec.isValid() && ! (model->getSpec() == spec))
        model->prepare (spec);

    models.publish (std::move (model));
}

void NamAmpBlock::installBeforePlaying (std::unique_ptr<NamModel> model)
{
    jassert (model != nullptr && current == nullptr && ! fading);

    const auto& spec = getPreparedSpec();

    if (spec.isValid() && ! (model->getSpec() == spec))
        model->prepare (spec);

    // Nothing is processing this block yet, so taking the model straight off
    // the queue here (instead of on the audio thread) is safe.
    models.publish (std::move (model));
    current = models.takeNext();
}

juce::File NamAmpBlock::getLoadedFile() const
{
    if (auto* m = models.getLatest())
        return m->getFile();

    return {};
}

juce::String NamAmpBlock::getLoadedFileInfo() const
{
    auto* m = models.getLatest();

    if (m == nullptr)
        return "No amp loaded: guitar passes through";

    juce::String info;
    info << juce::String (m->getModelRate() / 1000.0, 1) << " kHz";

    if (m->isResampling())
        info << " (converted)";

    if (m->getMeasuredCpuLoad() > 0.0)
        info << ", ~" << juce::String (m->getMeasuredCpuLoad() * 100.0, 1) << " % CPU";

    if (! m->getLoudnessDb().has_value())
        info << ", no level data";

    return info;
}

int NamAmpBlock::getLatencySamples() const noexcept
{
    if (auto* m = models.getLatest())
        return m->getLatencySamples();

    return 0;
}

//==============================================================================
void NamAmpBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    fadeLength = juce::jmax (1, (int) (modelFadeSeconds * spec.sampleRate));
    fadeBuffer.assign ((size_t) spec.maxBlockSize, 0.0f);
    normaliseCurve.assign ((size_t) spec.maxBlockSize, 0.0f);

    // Audio is stopped: re-prepare every model we own (playing or waiting).
    models.forEachOwned ([&] (NamModel& m)
    {
        if (! (m.getSpec() == spec))
            m.prepare (spec);
    });
}

void NamAmpBlock::resetBlock() noexcept
{
    for (auto& f : toneStack)
        f.reset();

    toneValid = false;
}

void NamAmpBlock::updateToneStack (int numSamples) noexcept
{
    const auto moving = bass.isSmoothing() || mid.isSmoothing() || treble.isSmoothing();

    if (! moving && toneValid)
        return;

    bass.skip (numSamples);
    mid.skip (numSamples);
    treble.skip (numSamples);

    using C = dsp::BiquadCoefficients;
    toneStack[0].setCoefficients (C::lowShelf  (sampleRate, bassHz, bass.currentValue()));
    toneStack[1].setCoefficients (C::peak      (sampleRate, midHz, midQ, mid.currentValue()));
    toneStack[2].setCoefficients (C::highShelf (sampleRate, trebleHz, treble.currentValue()));
    toneValid = true;
}

void NamAmpBlock::runModel (NamModel* model, float* samples, const float* normaliseAmount, int numSamples) noexcept
{
    if (model == nullptr)
        return;   // no amp: the guitar passes through

    model->process (samples, samples, numSamples);

    // Level match: bring the capture to a common loudness, blended by the switch.
    if (const auto loudness = model->getLoudnessDb())
    {
        const auto matchGain = juce::Decibels::decibelsToGain (normalisedLoudnessDb - (float) *loudness);

        for (int i = 0; i < numSamples; ++i)
            samples[i] *= 1.0f + (matchGain - 1.0f) * normaliseAmount[i];
    }
}

void NamAmpBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                const ProcessContext&) noexcept
{
    // A new model arrived: start a crossfade from whatever was playing.
    while (auto* next = models.takeNext())
    {
        if (fading)
            models.retire (fadingOut);   // a third model while fading: cut the oldest

        fadingOut = current;
        current = next;
        fading = true;
        fadePosition = 0;
    }

    float* x = channels[0];   // amps are mono

    for (int i = 0; i < numSamples; ++i)
        x[i] *= juce::Decibels::decibelsToGain (inputGain.nextValue());

    for (int i = 0; i < numSamples; ++i)
        normaliseCurve[(size_t) i] = normalise.nextValue();

    if (fading)
    {
        std::copy (x, x + numSamples, fadeBuffer.data());
        runModel (fadingOut, fadeBuffer.data(), normaliseCurve.data(), numSamples);
        runModel (current, x, normaliseCurve.data(), numSamples);

        // Linear crossfade: two amps fed the same guitar are strongly
        // correlated, so this keeps the level steady (equal-power would bump it).
        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = juce::jmin (1.0f, (float) (fadePosition + i) / (float) fadeLength);
            x[i] = fadeBuffer[(size_t) i] + (x[i] - fadeBuffer[(size_t) i]) * t;
        }

        fadePosition += numSamples;

        if (fadePosition >= fadeLength)
        {
            models.retire (fadingOut);
            fadingOut = nullptr;
            fading = false;
        }
    }
    else
    {
        runModel (current, x, normaliseCurve.data(), numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
        x[i] *= juce::Decibels::decibelsToGain (outputGain.nextValue());

    for (int offset = 0; offset < numSamples; offset += toneUpdateInterval)
    {
        const auto chunk = juce::jmin (toneUpdateInterval, numSamples - offset);
        updateToneStack (chunk);

        for (auto& f : toneStack)
            f.process (x + offset, 0, chunk);
    }

    for (int ch = 1; ch < numChannels; ++ch)
        std::copy (x, x + numSamples, channels[ch]);
}

} // namespace encore
