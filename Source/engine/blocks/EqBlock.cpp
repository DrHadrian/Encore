// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/EqBlock.h"

namespace encore
{

namespace
{
    constexpr double bassShelfHz = 120.0;
    constexpr double trebleShelfHz = 3500.0;
    constexpr double butterworthQ = 0.7071;
    constexpr int updateInterval = 32;    // samples between coefficient updates while knobs move

    // Frequency knobs feel natural with more resolution at the low end.
    juce::NormalisableRange<float> frequencyRange (float low, float high)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (std::sqrt (low * high));
        return r;
    }
}

EqBlock::EqBlock()
    : Block (typeId, "EQ"),
      lowCut       (addParameter ({ "lowCut",   "Low cut",   frequencyRange (20.0f, 400.0f),    20.0f,    "Hz", 0 })),
      bassGain     (addParameter ({ "bass",     "Bass",      { -12.0f, 12.0f },                  0.0f,    "dB", 1 })),
      midGain      (addParameter ({ "mid",      "Mid",       { -12.0f, 12.0f },                  0.0f,    "dB", 1 })),
      midFrequency (addParameter ({ "midFreq",  "Mid freq",  frequencyRange (200.0f, 5000.0f),  800.0f,   "Hz", 0 })),
      midQ         (addParameter ({ "midQ",     "Mid width", frequencyRange (0.3f, 4.0f),       0.9f,     "Q",  2 })),
      trebleGain   (addParameter ({ "treble",   "Treble",    { -12.0f, 12.0f },                  0.0f,    "dB", 1 })),
      highCut      (addParameter ({ "highCut",  "High cut",  frequencyRange (2000.0f, 20000.0f), 20000.0f, "Hz", 0 }))
{
}

void EqBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    coefficientsValid = false;
}

void EqBlock::resetBlock() noexcept
{
    for (auto& s : stages)
        s.reset();

    coefficientsValid = false;
}

bool EqBlock::anyParameterSmoothing() const noexcept
{
    for (auto& p : getParameters())
        if (p->isSmoothing())
            return true;

    return false;
}

void EqBlock::updateCoefficients() noexcept
{
    const auto lowCutHz = (double) lowCut.currentValue();
    const auto highCutHz = (double) highCut.currentValue();

    // A filter at its "off" extreme, or a flat shelf/peak, is skipped entirely.
    // It switches over at a setting where it is inaudibly close to flat, and a
    // stage coming back starts from clean state, so switching never clicks.
    const std::array<bool, numStages> wanted {
        lowCutHz > 20.5,
        std::abs (bassGain.currentValue()) > 0.05f,
        std::abs (midGain.currentValue()) > 0.05f,
        std::abs (trebleGain.currentValue()) > 0.05f,
        highCutHz < 19900.0
    };

    for (size_t s = 0; s < (size_t) numStages; ++s)
    {
        if (wanted[s] && ! stageActive[s])
            stages[s].reset();

        stageActive[s] = wanted[s];
    }

    using C = dsp::BiquadCoefficients;
    stages[lowCutStage].setCoefficients  (C::highPass  (sampleRate, lowCutHz, butterworthQ));
    stages[bassStage].setCoefficients    (C::lowShelf  (sampleRate, bassShelfHz, bassGain.currentValue()));
    stages[midStage].setCoefficients     (C::peak      (sampleRate, midFrequency.currentValue(), midQ.currentValue(), midGain.currentValue()));
    stages[trebleStage].setCoefficients  (C::highShelf (sampleRate, trebleShelfHz, trebleGain.currentValue()));
    stages[highCutStage].setCoefficients (C::lowPass   (sampleRate, highCutHz, butterworthQ));

    coefficientsValid = true;
}

void EqBlock::processBlock (float* const* channels, int numChannels, int numSamples,
                            const ProcessContext&) noexcept
{
    for (int offset = 0; offset < numSamples;)
    {
        const auto smoothing = anyParameterSmoothing();
        const auto chunk = smoothing ? juce::jmin (updateInterval, numSamples - offset) : numSamples - offset;

        if (smoothing || ! coefficientsValid)
        {
            for (auto& p : getParameters())
                p->skip (chunk);

            updateCoefficients();
        }

        for (int s = 0; s < numStages; ++s)
            if (stageActive[(size_t) s])
                for (int ch = 0; ch < numChannels; ++ch)
                    stages[(size_t) s].process (channels[ch] + offset, ch, chunk);

        offset += chunk;
    }
}

} // namespace encore
