// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/EngineProbeBlock.h"

namespace encore
{

namespace
{
    // Index 0 is Off; the rest follow dsp::FilterShape in order.
    const juce::StringArray shapeNames { "Off", "Bell", "Low shelf", "High shelf", "Low cut", "High cut",
                                         "Notch", "Band pass", "All pass" };

    juce::StringArray slopeNames()
    {
        juce::StringArray names;

        for (int s = 6; s <= dsp::FilterBandDesign::maxSlopeDbPerOctave; s += 6)
            names.add (juce::String (s) + " dB/oct");

        return names;
    }

    Parameter::Spec logSpec (const char* id, const char* name, float lo, float hi, float def, const char* unit, int decimals)
    {
        juce::NormalisableRange<float> range (lo, hi);
        range.setSkewForCentre (std::sqrt (lo * hi));
        return { id, name, range, def, unit, decimals };
    }
}

EngineProbeBlock::EngineProbeBlock()
    : Block (typeId, "Engine probe"),
      shape     (addParameter (Parameter::Spec::choice ("shape", "Shape", shapeNames, 0))),
      frequency (addParameter (logSpec ("freq", "Frequency", 20.0f, 20000.0f, 1000.0f, "Hz", 0))),
      gain      (addParameter ({ "gain", "Gain", { -30.0f, 30.0f }, 0.0f, "dB", 1 })),
      q         (addParameter (logSpec ("q", "Q", 0.1f, 10.0f, 0.71f, {}, 2))),
      slope     (addParameter (Parameter::Spec::choice ("slope", "Slope", slopeNames(), 1))),
      drive     (addParameter ({ "drive", "Drive", { 0.0f, 24.0f }, 0.0f, "dB", 1 })),
      quality   (addParameter (Parameter::Spec::choice ("quality", "Quality", { "Live", "High", "Max" }, 0))),
      trim      (addParameter ({ "trim", "Trim", { -24.0f, 24.0f }, 0.0f, "dB", 1 }))
{
}

dsp::FilterSettings EngineProbeBlock::settingsFrom (int shapeIndex, int slopeIndex, float f, float g, float qValue) const noexcept
{
    dsp::FilterSettings s;
    s.shape = (dsp::FilterShape) juce::jmax (0, shapeIndex - 1);
    s.frequency = f;
    s.gainDb = shapeIndex == 0 ? 0.0 : g;   // Off = a bell at 0 dB: no sections
    s.q = qValue;
    s.slopeDbPerOctave = 6 * (slopeIndex + 1);

    if (shapeIndex == 0)
        s.shape = dsp::FilterShape::bell;

    return s;
}

dsp::FilterSettings EngineProbeBlock::getFilterSettings() const noexcept
{
    return settingsFrom (shape.getChoiceIndex(), slope.getChoiceIndex(),
                         frequency.getValue(), gain.getValue(), q.getValue());
}

int EngineProbeBlock::getLatencySamples() const noexcept
{
    return driveActive.load (std::memory_order_relaxed) ? oversampler.getLatencySamples() : 0;
}

void EngineProbeBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    fade.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
    driveFade.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
    oversampler.setQuality ((dsp::Quality) quality.getChoiceIndex());
    oversampler.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
}

void EngineProbeBlock::resetBlock() noexcept
{
    appliedShape = shape.getChoiceIndex();
    appliedSlope = slope.getChoiceIndex();
    appliedSettings = settingsFrom (appliedShape, appliedSlope, frequency.currentValue(), gain.currentValue(), q.currentValue());
    band.setDesign (dsp::FilterBandDesign::make (appliedSettings, sampleRate));
    band.reset();
    fade.reset();

    oversampler.reset();
    driveFade.reset();
    driveActive.store (drive.getValue() > 0.0f, std::memory_order_relaxed);
}

void EngineProbeBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                     const ProcessContext&) noexcept
{
    // A new shape or slope: keep playing the old one beside the new for 10 ms.
    // (A change during a fade waits for the next block after it ends.)
    const auto newShape = juce::roundToInt (shape.skip (numSamples));
    const auto newSlope = juce::roundToInt (slope.skip (numSamples));

    if ((newShape != appliedShape || newSlope != appliedSlope) && ! fade.isActive())
    {
        oldBand = band;   // the old setting with its current state (plain data, no allocation)
        appliedShape = newShape;
        appliedSlope = newSlope;
        appliedSettings = settingsFrom (appliedShape, appliedSlope, frequency.currentValue(), gain.currentValue(), q.currentValue());
        band.setDesign (dsp::FilterBandDesign::make (appliedSettings, sampleRate));
        fade.start();
    }

    const auto fading = fade.isActive();

    if (fading)
        fade.copyInput (channels, numChannels, numSamples);

    for (int start = 0; start < numSamples; start += updateInterval)
    {
        const auto length = juce::jmin (updateInterval, numSamples - start);

        // The continuous values, once per chunk: redesign only if they moved.
        const auto settings = settingsFrom (appliedShape, appliedSlope, frequency.skip (length), gain.skip (length), q.skip (length));

        if (! (settings == appliedSettings))
        {
            appliedSettings = settings;
            band.setDesign (dsp::FilterBandDesign::make (settings, sampleRate));
        }

        float* chunk[maxChannels] {};

        for (int ch = 0; ch < numChannels; ++ch)
            chunk[ch] = channels[ch] + start;

        band.process (chunk, numChannels, length);

        if (fading)
        {
            float* oldChunk[maxChannels] {};

            for (int ch = 0; ch < numChannels; ++ch)
                oldChunk[ch] = fade.oldChannels()[ch] + start;

            oldBand.process (oldChunk, numChannels, length);
        }
    }

    if (fading)
        fade.mix (channels, numChannels, numSamples);

    // The drive, oversampled. Off (0 dB) and on cross over with a 10 ms fade.
    driveGain = juce::Decibels::decibelsToGain (drive.skip (numSamples));
    oversampler.setQuality ((dsp::Quality) juce::roundToInt (quality.skip (numSamples)));

    const auto wantDrive = driveGain > 1.0f;
    const auto active = driveActive.load (std::memory_order_relaxed);

    if (wantDrive != active && ! driveFade.isActive())
    {
        if (wantDrive)
            oversampler.reset();

        driveWasActive = active;
        driveActive.store (wantDrive, std::memory_order_relaxed);
        driveFade.start();
    }

    const auto saturate = [this] (float* const* up, int n, int length, double, int) noexcept
    {
        const auto g = driveGain;

        for (int ch = 0; ch < n; ++ch)
            for (int i = 0; i < length; ++i)
                up[ch][i] = std::tanh (g * up[ch][i]) / g;   // unity gain for small signals
    };

    const auto driveFading = driveFade.isActive();

    if (driveFading)
    {
        driveFade.copyInput (channels, numChannels, numSamples);

        if (driveWasActive)
            oversampler.process (driveFade.oldChannels(), numChannels, numSamples, saturate);
    }

    if (driveActive.load (std::memory_order_relaxed))
        oversampler.process (channels, numChannels, numSamples, saturate);

    if (driveFading)
        driveFade.mix (channels, numChannels, numSamples);

    // Trim last. At 0 dB, and not moving, nothing is touched: bit-exact.
    if (! trim.isSmoothing() && juce::exactlyEqual (trim.currentValue(), 0.0f))
    {
        trim.skip (numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = juce::Decibels::decibelsToGain (trim.nextValue());

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }
}

} // namespace encore
