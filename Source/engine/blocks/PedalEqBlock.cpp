// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/PedalEqBlock.h"

namespace encore
{

namespace
{
    juce::String bandName (double hz)
    {
        return hz < 1000.0 ? juce::String ((int) hz) + " Hz" : juce::String (hz / 1000.0, 1) + " kHz";
    }

    Parameter::Spec slider (const char* id, const juce::String& name)
    {
        return { id, name, { -PedalEqBlock::sliderRangeDb, PedalEqBlock::sliderRangeDb }, 0.0f, "dB", 1 };
    }

    // Factory presets (ids stable once merged; values Proposed, EQ-PLAN §4).
    constexpr PresetValue midPush[]    { { "g800", 6.0f }, { "g1k6", 4.0f } };
    constexpr PresetValue scoop[]      { { "g400", -5.0f }, { "g800", -6.0f } };
    constexpr PresetValue cleanBoost[] { { "input", (float) PedalEqBlock::Input::boost }, { "level", 6.0f } };
    constexpr PresetValue bright[]     { { "g3k2", 4.0f }, { "g6k4", 3.0f } };
    constexpr PresetValue thicken[]    { { "g100", 3.0f }, { "g200", 4.0f }, { "g6k4", -3.0f } };

    constexpr FactoryPreset factoryPresets[] {
        { "flat",       "Flat",        {} },
        { "midPush",    "Mid push",    midPush },
        { "scoop",      "Scoop",       scoop },
        { "cleanBoost", "Clean boost", cleanBoost },
        { "bright",     "Bright",      bright },
        { "thicken",    "Thicken",     thicken },
    };
}

std::span<const FactoryPreset> PedalEqBlock::getFactoryPresets() const noexcept
{
    return factoryPresets;
}

bool PedalEqBlock::isPresetParameter (const Parameter& p) const noexcept
{
    // The face, not the setup.
    return &p != &quality && &p != &captureLevel && &p != &realClip;
}

PedalEqBlock::PedalEqBlock()
    : Block (typeId, "Pedal EQ"),
      bands        (addBands()),
      level        (addParameter (slider ("level", "Level"))),
      qMode        (addParameter (Parameter::Spec::choice ("qMode", "Q", { "Classic", "Wide" }, (int) QMode::classic))),
      input        (addParameter (Parameter::Spec::choice ("input", "Input", { "Pad -6 dB", "Normal", "Boost +6 dB" }, (int) Input::normal))),
      quality      (addParameter (Parameter::Spec::choice ("quality", "Quality", { "Live", "High", "Max" }, 0))),
      captureLevel (addParameter (Parameter::Spec::choice ("captureLevel", "Capture level", { "As captured", "Level-matched" },
                                                           (int) CaptureLevel::asCaptured))),
      realClip     (addParameter (Parameter::Spec::choice ("realClip", "Real pedal clipping", { "Off", "On" }, (int) RealClip::off)))
{
}

std::array<Parameter*, PedalEqBlock::numBands> PedalEqBlock::addBands()
{
    // The sliders lead the parameter list, as on the pedal (left to right), then Level.
    std::array<Parameter*, numBands> made {};

    for (size_t b = 0; b < (size_t) numBands; ++b)
        made[b] = &addParameter (slider (bandIds[b], bandName (bandFrequencies[b])));

    return made;
}

float PedalEqBlock::inputGainFor (Input setting) noexcept
{
    switch (setting)
    {
        case Input::pad:    return juce::Decibels::decibelsToGain (padDb);
        case Input::boost:  return juce::Decibels::decibelsToGain (boostDb);
        case Input::normal: break;
    }

    return 1.0f;
}

double PedalEqBlock::qFor (QMode mode, double gainDb) noexcept
{
    if (mode == QMode::wide)
        return qAtZero;

    const auto share = juce::jlimit (0.0, 1.0, std::abs (gainDb) / (double) sliderRangeDb);
    return qAtZero + (qAtFullScale - qAtZero) * share;
}

dsp::FilterBandDesign PedalEqBlock::designFor (int band, double gainDb, QMode mode, double rate) noexcept
{
    dsp::FilterSettings s;
    s.shape = dsp::FilterShape::bell;   // decramped: matched to the analog bell up to Nyquist
    s.frequency = bandFrequencies[(size_t) band];
    s.gainDb = gainDb;                  // 0 dB = no section: costs nothing, bit-exact
    s.q = qFor (mode, gainDb);
    return dsp::FilterBandDesign::make (s, rate);
}

std::array<dsp::FilterBandDesign, PedalEqBlock::numBands> PedalEqBlock::getBandDesigns (double rate) const noexcept
{
    std::array<dsp::FilterBandDesign, numBands> designs;
    const auto mode = (QMode) qMode.getChoiceIndex();

    for (int b = 0; b < numBands; ++b)
        designs[(size_t) b] = designFor (b, bands[(size_t) b]->getValue(), mode, rate);

    return designs;
}

int PedalEqBlock::getLatencySamples() const noexcept
{
    return isClipInPath() ? oversampler.getLatencySamples() : 0;
}

//==============================================================================
void PedalEqBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    qFade.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
    inputGain.reset (spec.sampleRate, dsp::Crossfade::defaultSeconds);

    oversampler.setQuality ((dsp::Quality) quality.getChoiceIndex());
    oversampler.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
    clipFade.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate, clipFadeSeconds);
    dcBlocker.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, dcBlockerHz, 0.0, 0.7071, 6 }, spec.sampleRate));
    hotHoldSamples = juce::roundToInt (hotHoldSeconds * spec.sampleRate);

    maxBlock = spec.maxBlockSize;
    clipHistory.setSize (maxChannels, clipWarmupSamples);
    clipWarmup.setSize (maxChannels, clipWarmupSamples);
}

void PedalEqBlock::resetBlock() noexcept
{
    appliedQ = (QMode) qMode.getChoiceIndex();

    for (int b = 0; b < numBands; ++b)
    {
        appliedGains[(size_t) b] = bands[(size_t) b]->currentValue();
        filters[(size_t) b].setDesign (designFor (b, appliedGains[(size_t) b], appliedQ, sampleRate));
        filters[(size_t) b].reset();
    }

    qFade.reset();
    inputGain.setCurrentAndTargetValue (inputGainFor ((Input) input.getChoiceIndex()));

    oversampler.reset();
    dcBlocker.reset();
    clipFade.reset();
    clipHistory.clear();
    clipInPath.store (clipNeeded(), std::memory_order_relaxed);
    hotSamplesLeft = 0;
    hot.store (false, std::memory_order_relaxed);
}

bool PedalEqBlock::clipNeeded() const noexcept
{
    // Real pedal clipping: always in the path, so a hot input clips too.
    if (realClip.getChoiceIndex() == (int) RealClip::on)
        return true;

    // Otherwise only when the settings can push the signal into the knee:
    // Input Boost, or a slider above 0 dB (now, or where it is heading).
    if (inputGain.getTargetValue() > 1.0f || inputGain.getCurrentValue() > 1.0f)
        return true;

    for (size_t b = 0; b < (size_t) numBands; ++b)
        if (appliedGains[b] > 0.0 || bands[b]->getValue() > 0.0f)
            return true;

    return false;
}

void PedalEqBlock::runClip (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto positiveHot = (float) softClip.getPositiveOneDbPoint();
    const auto negativeHot = -(float) softClip.getNegativeOneDbPoint();

    oversampler.process (channels, numChannels, numSamples, [this, positiveHot, negativeHot]
                         (float* const* up, int n, int length, double, int) noexcept
    {
        auto worked = false;

        for (int ch = 0; ch < n; ++ch)
        {
            auto* x = up[ch];

            for (int i = 0; i < length; ++i)
            {
                worked = worked || x[i] > positiveHot || x[i] < negativeHot;
                x[i] = softClip.process (x[i]);
            }
        }

        clipWorkedThisBlock = clipWorkedThisBlock || worked;
    });

    dcBlocker.process (channels, numChannels, numSamples);
}

void PedalEqBlock::rememberClipInput (const float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto keep = clipHistory.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* h = clipHistory.getWritePointer (ch);

        if (numSamples >= keep)
        {
            std::copy (channels[ch] + numSamples - keep, channels[ch] + numSamples, h);
        }
        else
        {
            std::copy (h + numSamples, h + keep, h);
            std::copy (channels[ch], channels[ch] + numSamples, h + keep - numSamples);
        }
    }
}

void PedalEqBlock::warmUpClip (int numChannels) noexcept
{
    oversampler.reset();
    dcBlocker.reset();

    const auto worked = clipWorkedThisBlock;   // the history doesn't count for HOT

    for (int start = 0; start < clipWarmupSamples; start += maxBlock)
    {
        const auto length = juce::jmin (maxBlock, clipWarmupSamples - start);
        float* chunk[maxChannels] {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            clipWarmup.copyFrom (ch, 0, clipHistory, ch, start, length);
            chunk[ch] = clipWarmup.getWritePointer (ch);
        }

        runClip (chunk, numChannels, length);
    }

    clipWorkedThisBlock = worked;
}

void PedalEqBlock::processClip (float* const* channels, int numChannels, int numSamples) noexcept
{
    oversampler.setQuality ((dsp::Quality) juce::roundToInt (quality.skip (numSamples)));
    realClip.skip (numSamples);

    const auto want = clipNeeded();
    const auto active = isClipInPath();

    clipWorkedThisBlock = false;

    if (want != active && ! clipFade.isActive())
    {
        if (want)
            warmUpClip (numChannels);   // comes in settled, from the recent signal

        clipWasInPath = active;
        clipInPath.store (want, std::memory_order_relaxed);   // the latency report follows
        clipFade.start();
    }

    const auto fading = clipFade.isActive();

    if (! isClipInPath() && ! fading)
        rememberClipInput (channels, numChannels, numSamples);

    if (fading)
    {
        clipFade.copyInput (channels, numChannels, numSamples);

        if (clipWasInPath)
            runClip (clipFade.oldChannels(), numChannels, numSamples);
    }

    if (isClipInPath())
        runClip (channels, numChannels, numSamples);

    if (fading)
        clipFade.mix (channels, numChannels, numSamples);

    hotSamplesLeft = clipWorkedThisBlock ? hotHoldSamples : juce::jmax (0, hotSamplesLeft - numSamples);
    hot.store (hotSamplesLeft > 0, std::memory_order_relaxed);
}

void PedalEqBlock::processInput (float* const* channels, int numChannels, int numSamples) noexcept
{
    inputGain.setTargetValue (inputGainFor ((Input) juce::roundToInt (input.skip (numSamples))));

    if (! inputGain.isSmoothing())
    {
        const auto g = inputGain.getCurrentValue();

        if (! juce::exactlyEqual (g, 1.0f))   // Normal touches nothing: bit-exact
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply (channels[ch], g, numSamples);

        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = inputGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }
}

void PedalEqBlock::processLevel (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! level.isSmoothing())
    {
        const auto db = level.skip (numSamples);

        if (! juce::exactlyEqual (db, 0.0f))   // 0 dB touches nothing: bit-exact
        {
            const auto g = juce::Decibels::decibelsToGain (db);

            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply (channels[ch], g, numSamples);
        }

        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = juce::Decibels::decibelsToGain (level.nextValue());

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }
}

void PedalEqBlock::processBands (float* const* channels, int numChannels, int numSamples) noexcept
{
    // A new Q mode: keep the old one playing beside the new for 10 ms.
    const auto newQ = (QMode) juce::roundToInt (qMode.skip (numSamples));

    if (newQ != appliedQ && ! qFade.isActive())
    {
        oldFilters = filters;   // the old setting with its state (plain data, no allocation)
        appliedQ = newQ;

        for (int b = 0; b < numBands; ++b)
            filters[(size_t) b].setDesign (designFor (b, appliedGains[(size_t) b], appliedQ, sampleRate));

        qFade.start();
    }

    const auto fading = qFade.isActive();

    if (fading)
        qFade.copyInput (channels, numChannels, numSamples);

    for (int start = 0; start < numSamples; start += updateInterval)
    {
        const auto length = juce::jmin (updateInterval, numSamples - start);
        float* chunk[maxChannels] {};
        float* oldChunk[maxChannels] {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            chunk[ch] = channels[ch] + start;
            oldChunk[ch] = fading ? qFade.oldChannels()[ch] + start : nullptr;
        }

        for (int b = 0; b < numBands; ++b)
        {
            // A slider that moved is redesigned for the end of this chunk and
            // glides there sample by sample (Classic Q follows its gain).
            const auto gain = (double) bands[(size_t) b]->skip (length);

            if (! juce::exactlyEqual (gain, appliedGains[(size_t) b]))
            {
                appliedGains[(size_t) b] = gain;
                filters[(size_t) b].glideTo (designFor (b, gain, appliedQ, sampleRate), length);
            }

            filters[(size_t) b].process (chunk, numChannels, length);

            if (fading)
                oldFilters[(size_t) b].process (oldChunk, numChannels, length);
        }
    }

    if (fading)
        qFade.mix (channels, numChannels, numSamples);
}

void PedalEqBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                                 const ProcessContext&) noexcept
{
    // EQ-PLAN §3.1: Input -> character capture -> 7 bands -> soft clip -> Level.
    processInput (channels, numChannels, numSamples);
    processBands (channels, numChannels, numSamples);
    processClip (channels, numChannels, numSamples);
    processLevel (channels, numChannels, numSamples);
}

} // namespace encore
