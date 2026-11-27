// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/StudioEqBlock.h"

#include "engine/core/FactoryPresets.h"

#include <cmath>

namespace encore
{

namespace
{
    using Spec = Parameter::Spec;

    /** A log range (10 Hz .. 30 kHz, Q 0.025 .. 40, times in ms). */
    juce::NormalisableRange<float> logRange (float low, float high)
    {
        juce::NormalisableRange<float> r (low, high);
        r.setSkewForCentre (std::sqrt (low * high));
        return r;
    }

    Spec choice (const juce::String& id, const juce::String& name, juce::StringArray names, int defaultIndex, bool onPedal = true)
    {
        auto s = Spec::choice (id, name, std::move (names), defaultIndex);
        s.expressionAllowed = onPedal;
        return s;
    }

    Spec number (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float defaultValue,
                 const juce::String& unit, int decimals)
    {
        Spec s { id, name, range, defaultValue, unit, decimals };
        return s;
    }

    const juce::StringArray shapeNames { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt shelf", "All pass" };
    const juce::StringArray slopeNames { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct",
                                         "72 dB/oct", "96 dB/oct", "Brickwall" };
}

juce::String StudioEqBlock::slotId (int slot, const char* suffix)
{
    return "b" + juce::String (slot + 1).paddedLeft ('0', 2) + suffix;
}

StudioEqBlock::StudioEqBlock()
    : Block (typeId, "Studio EQ"),
      // Block-wide (EQ-PLAN §2.9). B4/B5/R1 bring phase, linearQuality,
      // character, drive, quality and cpuTight to life.
      phase         (addParameter (choice ("phase", "Phase", { "Zero latency", "Natural phase", "Linear phase" }, 0, false))),
      linearQuality (addParameter (choice ("linearQuality", "Linear phase quality", { "Low", "Medium", "High" }, 1, false))),
      character     (addParameter (choice ("character", "Character", { "Clean", "Soft", "Warm" }, 0))),
      drive         (addParameter (number ("drive", "Drive", { 0.0f, 24.0f }, 0.0f, "dB", 1))),
      quality       (addParameter (choice ("quality", "Quality", { "Live", "High", "Max" }, 0, false))),
      output        (addParameter (number ("output", "Output", { -36.0f, 36.0f }, 0.0f, "dB", 1))),
      pan           (addParameter (number ("pan", "Pan", { -100.0f, 100.0f }, 0.0f, "%", 0))),
      width         (addParameter (number ("width", "Width", { 0.0f, 200.0f }, 100.0f, "%", 0))),
      autoGain      (addParameter (choice ("autoGain", "Auto gain", { "Off", "Curve", "Smart" }, 0))),
      gainScale     (addParameter (number ("gainScale", "Gain scale", { -100.0f, 200.0f }, 100.0f, "%", 0))),
      invert        (addParameter (choice ("invert", "Phase invert", { "Off", "On" }, 0))),
      cpuTight      (addParameter (choice ("cpuTight", "When CPU is tight", { "Keep the sound", "Make it lighter" }, 0, false))),
      displayRange  (addParameter (choice ("range", "Display range", { "3 dB", "6 dB", "12 dB", "30 dB" }, 2, false)))
{
    for (int s = 0; s < numSlots; ++s)
    {
        auto& p = slots[(size_t) s].p;
        const auto id = [s] (const char* suffix) { return slotId (s, suffix); };
        const auto label = [s] (const char* what) { return "Band " + juce::String (s + 1) + " " + what; };

        p.used   = &addParameter (choice (id ("Used"), label ("in use"), { "Free", "In use" }, 0, false));
        p.on     = &addParameter (choice (id ("On"), label ("on"), { "Off", "On" }, 1));
        p.shape  = &addParameter (choice (id ("Shape"), label ("shape"), shapeNames, (int) Shape::bell));
        p.freq   = &addParameter (number (id ("Freq"), label ("frequency"), logRange (10.0f, 30000.0f), defaultFrequency, "Hz", 0));
        p.gain   = &addParameter (number (id ("Gain"), label ("gain"), { -30.0f, 30.0f }, 0.0f, "dB", 1));
        p.q      = &addParameter (number (id ("Q"), label ("Q"), logRange (0.025f, 40.0f), 1.0f, {}, 2));
        p.slope  = &addParameter (choice (id ("Slope"), label ("slope"), slopeNames, 1));

        // B4: dynamics, sidechain and placement (saved now, no sound yet).
        p.place      = &addParameter (choice (id ("Place"), label ("placement"), { "Stereo", "Left", "Right", "Mid", "Side" }, 0, false));
        p.mode       = &addParameter (choice (id ("Mode"), label ("mode"), { "Static", "Dynamic", "Spectral" }, 0, false));
        p.range      = &addParameter (number (id ("Range"), label ("range"), { -30.0f, 30.0f }, 0.0f, "dB", 1));
        p.thresh     = &addParameter (number (id ("Thresh"), label ("threshold"), { -61.0f, 0.0f }, -61.0f, "dB", 1));   // -61 = Auto
        p.attack     = &addParameter (number (id ("Attack"), label ("attack"), logRange (0.1f, 500.0f), 10.0f, "ms", 1));
        p.release    = &addParameter (number (id ("Release"), label ("release"), logRange (5.0f, 5000.0f), 100.0f, "ms", 0));
        p.side       = &addParameter (choice (id ("Side"), label ("sidechain"), { "This band", "Dry guitar" }, 0, false));
        p.sideFilter = &addParameter (choice (id ("SideFilter"), label ("sidechain filter"), { "Follows the band", "Own" }, 0, false));
    }
}

//==============================================================================
dsp::FilterSettings StudioEqBlock::filterSettingsFor (const BandSettings& band, double gainScalePercent) noexcept
{
    static constexpr dsp::FilterShape shapes[] = {
        dsp::FilterShape::bell, dsp::FilterShape::lowShelf, dsp::FilterShape::highShelf, dsp::FilterShape::lowCut,
        dsp::FilterShape::highCut, dsp::FilterShape::notch, dsp::FilterShape::bandPass, dsp::FilterShape::tiltShelf,
        dsp::FilterShape::allPass
    };

    dsp::FilterSettings s;
    const auto shape = juce::jlimit (0, (int) std::size (shapes) - 1, (int) band.shape);
    s.shape = shapes[shape];
    s.frequency = band.frequency;
    s.gainDb = isScaled (shape) ? band.gainDb * gainScalePercent * 0.01 : 0.0;
    s.q = band.q;
    s.slopeDbPerOctave = slopeValues[(size_t) juce::jlimit (0, numSlopes - 1, band.slope)];
    return s;
}

bool StudioEqBlock::isScaled (int shape) noexcept
{
    // Gain scale works on what has a gain: bells, shelves and the tilt.
    return shape == (int) Shape::bell || shape == (int) Shape::lowShelf || shape == (int) Shape::highShelf
           || shape == (int) Shape::tiltShelf;
}

StudioEqBlock::BandSettings StudioEqBlock::getBand (int slot) const noexcept
{
    const auto& p = slots[(size_t) slot].p;
    BandSettings b;
    b.used = p.used->getChoiceIndex() == 1;
    b.on = p.on->getChoiceIndex() == 1;
    b.shape = (Shape) p.shape->getChoiceIndex();
    b.frequency = p.freq->getValue();
    b.gainDb = p.gain->getValue();
    b.q = p.q->getValue();
    b.slope = p.slope->getChoiceIndex();
    return b;
}

int StudioEqBlock::getNumBandsInUse() const noexcept
{
    int n = 0;

    for (const auto& s : slots)
        n += s.p.used->getChoiceIndex() == 1 ? 1 : 0;

    return n;
}

std::vector<dsp::FilterBandDesign> StudioEqBlock::getBandDesigns (double rate) const
{
    std::vector<dsp::FilterBandDesign> designs;

    for (int s = 0; s < numSlots; ++s)
    {
        const auto band = getBand (s);

        if (band.used && band.on)
            designs.push_back (dsp::FilterBandDesign::make (filterSettingsFor (band, gainScale.getValue()), rate));
    }

    return designs;
}

//==============================================================================
void StudioEqBlock::prepareBlock (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    toggleSamples = juce::jmax (1, juce::roundToInt (toggleSeconds * sampleRate));

    for (auto& slot : slots)
    {
        slot.fade.prepare (maxChannels, spec.maxBlockSize, sampleRate);
        slot.freqRamp.reset (sampleRate, slot.p.freq->getSpec().rampSeconds);
        slot.qRamp.reset (sampleRate, slot.p.q->getSpec().rampSeconds);
    }

    resetBlock();
}

void StudioEqBlock::resetBlock() noexcept
{
    // A clean state at the parameters' values: every band designed for this
    // rate, nothing gliding or crossfading.
    const auto scale = (double) gainScale.getValue();

    for (auto& slot : slots)
    {
        slot.playing = slot.p.used->getChoiceIndex() == 1 && slot.p.on->getChoiceIndex() == 1;
        slot.shape = slot.p.shape->getChoiceIndex();
        slot.slope = slot.p.slope->getChoiceIndex();
        slot.toggleRemaining = 0;
        slot.fade.reset();
        slot.band.reset();
        slot.oldBand.reset();

        slot.snapValues();
        slot.band.setDesign (slot.playing ? designNow (slot, scale) : dsp::FilterBandDesign {});
    }

    gainScale.snapToTarget();
}

dsp::FilterBandDesign StudioEqBlock::designNow (const Slot& slot, double scale) const noexcept
{
    BandSettings b;
    b.shape = (Shape) slot.shape;
    b.frequency = slot.freqRamp.getCurrentValue();
    b.gainDb = slot.p.gain->currentValue();
    b.q = slot.qRamp.getCurrentValue();
    b.slope = slot.slope;
    return dsp::FilterBandDesign::make (filterSettingsFor (b, scale), sampleRate);
}

void StudioEqBlock::applyChoices (Slot& slot, double scale) noexcept
{
    const auto playing = slot.p.used->getChoiceIndex() == 1 && slot.p.on->getChoiceIndex() == 1;
    const auto shape = slot.p.shape->getChoiceIndex();
    const auto slope = slot.p.slope->getChoiceIndex();

    if (playing != slot.playing)
    {
        // On, off, in or out of use: glide to or from flat (no click).
        slot.playing = playing;
        slot.shape = shape;
        slot.slope = slope;

        if (playing)
            slot.snapValues();   // values set while it was off: start there

        slot.fade.reset();
        slot.band.glideTo (playing ? designNow (slot, scale) : dsp::FilterBandDesign {}, toggleSamples);
        slot.toggleRemaining = toggleSamples;
        return;
    }

    // A Shape or Slope change: this band only crossfades from its old setting
    // (one at a time; a change during a fade waits for it to finish).
    if (playing && (shape != slot.shape || slope != slot.slope) && ! slot.fade.isActive())
    {
        slot.oldBand = slot.band;
        slot.shape = shape;
        slot.slope = slope;
        slot.band.reset();
        slot.band.setDesign (designNow (slot, scale));
        slot.fade.start();
    }
}

void StudioEqBlock::processSlot (Slot& slot, float* const* channels, int numChannels, int numSamples) noexcept
{
    if (slot.fade.isActive())
    {
        slot.fade.copyInput (channels, numChannels, numSamples);
        slot.oldBand.process (slot.fade.oldChannels(), numChannels, numSamples);
        slot.band.process (channels, numChannels, numSamples);
        slot.fade.mix (channels, numChannels, numSamples);
        return;
    }

    slot.band.process (channels, numChannels, numSamples);
}

void StudioEqBlock::processBlock (float* const* channels, int numChannels, int numSamples, const ProcessContext&) noexcept
{
    numChannels = juce::jlimit (1, maxChannels, numChannels);
    const juce::ScopedNoDenormals noDenormals;

    // Gain scale moves every scaled band.
    const auto scaleMoving = gainScale.isSmoothing();
    auto scale = (double) gainScale.currentValue();
    auto anyMoving = scaleMoving;

    std::array<bool, numSlots> moving {};
    std::array<bool, numSlots> audible {};

    for (int s = 0; s < numSlots; ++s)
    {
        auto& slot = slots[(size_t) s];
        applyChoices (slot, scale);

        if (slot.playing)
        {
            slot.freqRamp.setTargetValue (slot.p.freq->getValue());
            slot.qRamp.setTargetValue (slot.p.q->getValue());
        }
        else if (slot.valuesMoving())
        {
            // Free or off: nothing runs once its glide to flat is over, and its
            // values aren't ramped while they rest.
            slot.snapValues();
        }

        audible[(size_t) s] = slot.playing || slot.band.isGliding() || slot.fade.isActive();
        moving[(size_t) s] = slot.playing && (slot.valuesMoving() || (scaleMoving && isScaled (slot.shape)));
        anyMoving = anyMoving || moving[(size_t) s];
    }

    if (! anyMoving)
    {
        // Nothing moves: each band plays the whole buffer (a band that changes
        // nothing has no sections, so it passes bit-exact).
        for (int s = 0; s < numSlots; ++s)
            if (audible[(size_t) s])
                processSlot (slots[(size_t) s], channels, numChannels, numSamples);

        for (auto& slot : slots)
            slot.toggleRemaining = juce::jmax (0, slot.toggleRemaining - numSamples);

        return;
    }

    // Something moves: in chunks, redesigning what moves at the chunk's end
    // and gliding there (no zipper noise: FilterBand::glideTo).
    for (int start = 0; start < numSamples; start += updateInterval)
    {
        const auto len = juce::jmin (updateInterval, numSamples - start);
        float* chunk[maxChannels] = { channels[0] + start, channels[numChannels > 1 ? 1 : 0] + start };

        if (scaleMoving)
            scale = (double) gainScale.skip (len);

        for (int s = 0; s < numSlots; ++s)
        {
            auto& slot = slots[(size_t) s];

            if (moving[(size_t) s])
            {
                slot.freqRamp.skip (len);
                slot.qRamp.skip (len);
                slot.p.gain->skip (len);

                // During a glide to flat's opposite (coming in), land with it.
                const auto glide = slot.toggleRemaining > len ? slot.toggleRemaining : len;
                const auto design = designNow (slot, scale);
                slot.band.glideTo (design, glide);   // the old setting of a crossfade keeps its design: it fades out
            }

            if (audible[(size_t) s])
                processSlot (slot, chunk, numChannels, len);

            slot.toggleRemaining = juce::jmax (0, slot.toggleRemaining - len);
        }
    }
}

//==============================================================================
std::span<const FactoryPreset> StudioEqBlock::getFactoryPresets() const noexcept
{
    return {};
}

bool StudioEqBlock::isPresetParameter (const Parameter& parameter) const noexcept
{
    // The sound, not the setup (EQ-PLAN §4, Proposed): the bands, gain scale,
    // output, auto gain, character and drive are; pan, width, invert, the
    // display range, Quality, the CPU setting and the phase mode aren't.
    for (const auto* setup : { &pan, &width, &invert, &displayRange, &quality, &cpuTight, &phase, &linearQuality })
        if (&parameter == setup)
            return false;

    return true;
}

} // namespace encore
