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

//==============================================================================
void PedalEqBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    qFade.prepare (maxChannels, spec.maxBlockSize, spec.sampleRate);
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
    processBands (channels, numChannels, numSamples);
}

} // namespace encore
