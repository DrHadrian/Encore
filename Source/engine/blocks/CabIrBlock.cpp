// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/CabIrBlock.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace encore
{

namespace
{
    constexpr int filterUpdateInterval = 32;

    juce::NormalisableRange<float> frequencyRange (float low, float high)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (std::sqrt (low * high));
        return r;
    }
}

CabIrBlock::CabIrBlock()
    : Block (typeId, "Cab"),
      lowCut  (addParameter ({ "lowCut",  "Low cut",  frequencyRange (20.0f, 400.0f),    20.0f,    "Hz", 0 })),
      highCut (addParameter ({ "highCut", "High cut", frequencyRange (2000.0f, 20000.0f), 20000.0f, "Hz", 0 })),
      level   (addParameter ({ "level",   "Level",    { -24.0f, 24.0f },                   0.0f,    "dB", 1 })),
      mix     (addParameter ({ "mix",     "Mix",      { 0.0f, 100.0f },                    100.0f,  "%",  0 })),
      convolution (juce::dsp::Convolution::Latency { 0 }, sharedQueue->queue)
{
}

CabIrBlock::~CabIrBlock() = default;

//==============================================================================
std::unique_ptr<CabIrBlock::ImpulseResponse> CabIrBlock::readFile (const juce::File& file, juce::String& error)
{
    error.clear();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "Not a readable audio file: " + file.getFileName()
                                    : "File not found: " + file.getFullPathName();
        return nullptr;
    }

    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = file.getFileName() + " is empty";
        return nullptr;
    }

    const auto maxSamples = (juce::int64) (maxIrSeconds * reader->sampleRate);
    const auto numSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);
    const auto numChannels = (int) juce::jmax (1u, reader->numChannels);

    juce::AudioBuffer<float> all (numChannels, numSamples);
    reader->read (&all, 0, numSamples, 0, true, true);

    auto ir = std::make_unique<ImpulseResponse>();
    ir->samples.setSize (1, numSamples);
    ir->samples.clear();

    // Stereo IRs (two mic positions) are mixed to mono: the chain is mono for now.
    for (int ch = 0; ch < numChannels; ++ch)
        ir->samples.addFrom (0, 0, all, ch, 0, numSamples, 1.0f / (float) numChannels);

    ir->sampleRate = reader->sampleRate;
    ir->file = file;
    ir->lengthSeconds = numSamples / reader->sampleRate;
    ir->wasShortened = reader->lengthInSamples > maxSamples;
    return ir;
}

void CabIrBlock::loadFileAsync (const juce::File& file, std::function<void (juce::String)> onDone)
{
    juce::WeakReference<Block> weakThis (this);

    jobs->add ([file, weakThis, onDone]
    {
        juce::String error;
        auto ir = readFile (file, error);
        auto holder = std::make_shared<std::unique_ptr<ImpulseResponse>> (std::move (ir));

        juce::MessageManager::callAsync ([weakThis, holder, error, onDone]
        {
            if (auto* block = dynamic_cast<CabIrBlock*> (weakThis.get()))
                if (*holder != nullptr)
                    block->installImpulseResponse (std::move (*holder));

            if (onDone != nullptr)
                onDone (error);
        });
    });
}

void CabIrBlock::installImpulseResponse (std::unique_ptr<ImpulseResponse> ir)
{
    jassert (ir != nullptr && ir->samples.getNumSamples() > 0);

    loadedFile = ir->file;
    loadedInfo = juce::String (ir->sampleRate / 1000.0, 1) + " kHz, "
               + juce::String (juce::roundToInt (ir->lengthSeconds * 1000.0)) + " ms"
               + (ir->wasShortened ? " (shortened to 1 s)" : "");

    irs.publish (std::move (ir));
}

//==============================================================================
void CabIrBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    dry.assign ((size_t) spec.maxBlockSize, 0.0f);

    // Allocates; also finishes loading any IR already handed to the engine,
    // resampled to the new rate.
    convolution.prepare ({ spec.sampleRate, (juce::uint32) spec.maxBlockSize, 1 });
    filtersValid = false;
}

void CabIrBlock::resetBlock() noexcept
{
    convolution.reset();

    for (auto& f : cuts)
        f.reset();

    filtersValid = false;
}

void CabIrBlock::updateFilters (int numSamples) noexcept
{
    if (filtersValid && ! lowCut.isSmoothing() && ! highCut.isSmoothing())
        return;

    const auto low = (double) lowCut.skip (numSamples);
    const auto high = (double) highCut.skip (numSamples);

    const std::array<bool, 2> wanted { low > 20.5, high < 19900.0 };

    for (size_t i = 0; i < 2; ++i)
    {
        if (wanted[i] && ! cutActive[i])
            cuts[i].reset();

        cutActive[i] = wanted[i];
    }

    cuts[0].setCoefficients (dsp::BiquadCoefficients::highPass (sampleRate, low, 0.7071));
    cuts[1].setCoefficients (dsp::BiquadCoefficients::lowPass (sampleRate, high, 0.7071));
    filtersValid = true;
}

void CabIrBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                               const ProcessContext&) noexcept
{
    // A new IR: give it to the engine (wait-free, no allocation: the buffer is
    // moved, not copied), then send the emptied holder back for deletion.
    while (auto* ir = irs.takeNext())
    {
        convolution.loadImpulseResponse (std::move (ir->samples), ir->sampleRate,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes,
                                         juce::dsp::Convolution::Normalise::yes);
        hasImpulseResponse = true;
        irs.retire (ir);
    }

    float* x = channels[0];   // cabs are mono for now

    if (hasImpulseResponse)
    {
        std::copy (x, x + numSamples, dry.data());

        float* single[] = { x };
        juce::dsp::AudioBlock<float> block (single, 1, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = mix.nextValue() * 0.01f;
            x[i] = dry[(size_t) i] + (x[i] - dry[(size_t) i]) * wet;
        }
    }

    for (int offset = 0; offset < numSamples; offset += filterUpdateInterval)
    {
        const auto chunk = juce::jmin (filterUpdateInterval, numSamples - offset);
        updateFilters (chunk);

        for (size_t f = 0; f < 2; ++f)
            if (cutActive[f])
                cuts[f].process (x + offset, 0, chunk);
    }

    for (int i = 0; i < numSamples; ++i)
        x[i] *= juce::Decibels::decibelsToGain (level.nextValue());

    for (int ch = 1; ch < numChannels; ++ch)
        std::copy (x, x + numSamples, channels[ch]);
}

} // namespace encore
