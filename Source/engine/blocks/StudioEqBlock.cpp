// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/StudioEqBlock.h"

#include "engine/core/FactoryPresets.h"

#include <cmath>
#include <limits>

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
    slots.resize ((size_t) numSlots);

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

    // B4: the sidechain filter's own frequency and Q (SideFilter Own), after
    // every B3 parameter so none of those moves.
    for (int s = 0; s < numSlots; ++s)
    {
        auto& p = slots[(size_t) s].p;
        const auto label = [s] (const char* what) { return "Band " + juce::String (s + 1) + " " + what; };
        p.sideFreq = &addParameter (number (slotId (s, "SideFreq"), label ("sidechain frequency"), logRange (20.0f, 20000.0f),
                                            defaultSideFrequency, "Hz", 0));
        p.sideQ    = &addParameter (number (slotId (s, "SideQ"), label ("sidechain Q"), logRange (0.1f, 10.0f), defaultSideQ, {}, 2));
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

bool StudioEqBlock::canBeDynamic (Shape shape) noexcept
{
    return isScaled ((int) shape);   // the shapes with a gain
}

double StudioEqBlock::movementDb (double overDb, double rangeDb) noexcept
{
    // 1 dB per dB over the threshold, with a soft knee of kneeDb centred on
    // it (quadratic, so the slope goes 0 -> 1 without a corner), up to |Range|.
    const auto half = 0.5 * kneeDb;
    const auto over = overDb <= -half ? 0.0 : overDb >= half ? overDb : (overDb + half) * (overDb + half) / (2.0 * kneeDb);
    const auto amount = std::min (over, std::abs (rangeDb));
    return rangeDb < 0.0 ? -amount : amount;
}

dsp::FilterSettings StudioEqBlock::sidechainSettingsFor (const BandSettings& band, SideFilter filter, double ownFrequency,
                                                         double ownQ, bool& filtered) noexcept
{
    dsp::FilterSettings s;
    s.slopeDbPerOctave = 12;
    filtered = true;

    if (filter == SideFilter::own)
    {
        s.shape = dsp::FilterShape::bandPass;
        s.frequency = ownFrequency;
        s.q = ownQ;
        return s;
    }

    s.frequency = band.frequency;

    switch (band.shape)
    {
        case Shape::lowShelf:  s.shape = dsp::FilterShape::highCut; s.q = 0.7071; break;   // a 12 dB/oct low pass: the lows it acts on
        case Shape::highShelf: s.shape = dsp::FilterShape::lowCut;  s.q = 0.7071; break;   // a 12 dB/oct high pass
        case Shape::tiltShelf: filtered = false; break;                                    // the whole spectrum
        default:               s.shape = dsp::FilterShape::bandPass; s.q = band.q; break;  // a Bell: its own frequency and Q
    }

    return s;
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

    // The Curve value's points: log-spaced over a guitar's range.
    for (int i = 0; i < curvePoints; ++i)
    {
        const auto f = curveLowHz * std::pow (curveHighHz / curveLowHz, i / (curvePoints - 1.0));
        const auto w = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
        pointCos[(size_t) i] = std::cos (w);
        pointCos2[(size_t) i] = std::cos (2.0 * w);
    }

    curveIntervalSamples = juce::jmax (1, juce::roundToInt (curveIntervalSeconds * sampleRate));

    // Dynamics: the EQ's input, the movement per chunk, per slot.
    eqInput.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    eqInputMidSide.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    maxChunks = juce::jmax (1, (spec.maxBlockSize + updateInterval - 1) / updateInterval);
    moves.assign ((size_t) numSlots * (size_t) maxChunks, 0.0f);
    playingInput.assign ((size_t) maxChunks, 0);
    playingDry.assign ((size_t) maxChunks, 0);
    detectorCoefficient = std::exp (-1000.0 / (detectorMs * sampleRate));

    // When CPU is tight: a new block starts at full; a new preparation (a
    // device restart mid-show) keeps what was taken.
    lighterWaitSamples = juce::roundToInt (lighterWaitSeconds * sampleRate);

    // Character: both paths ready for every Quality, so a change never allocates.
    maxBlock = juce::jmax (1, spec.maxBlockSize);

    for (auto& path : characterPaths)
    {
        path.oversampler.prepare (maxChannels, maxBlock, sampleRate);
        path.pre.set (sampleRate);
        path.post.set (sampleRate, dsp::LowEmphasis::defaultFrequency, dsp::LowEmphasis::defaultGainDb, true);
        path.dcBlocker.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::lowCut, dcBlockerHz, 0.0, 0.7071, 6 }, sampleRate));
    }

    characterFade.prepare (maxChannels, maxBlock, sampleRate, characterFadeSeconds);
    characterHistory.setSize (maxChannels, characterWarmupSamples);
    characterWarmup.setSize (maxChannels, juce::jmin (characterWarmupSamples, maxBlock));
    inputLoudness.prepare (sampleRate, smartWindowSeconds);
    bandsLoudness.prepare (sampleRate, smartWindowSeconds);
    autoGainDb.reset (sampleRate, autoGainRampSeconds);
    polarity.reset (sampleRate, invertFadeSeconds);

    for (auto* ramp : { &outputGain, &panLeft, &panRight, &widthAmount })
        ramp->reset (sampleRate, output.getSpec().rampSeconds);

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
        slot.place = slot.oldPlace = stereoNow ? slot.p.place->getChoiceIndex() : (int) Place::stereo;
        slot.toggleRemaining = 0;
        slot.fade.reset();
        slot.band.reset();
        slot.oldBand.reset();

        slot.snapValues();
        slot.band.setDesign (slot.playing ? designNow (slot, scale) : dsp::FilterBandDesign {});

        // Dynamics start at rest; Auto learns afresh.
        slot.sideBand.reset();
        slot.sideDesigned = false;
        slot.sideMs.fill (0.0);
        slot.autoMs = 0.0;
        slot.autoLearned = false;
        slot.moveDb = slot.designedMoveDb = 0.0;
        slot.dynamic = slot.moveChanged = false;
    }

    playingInputMs = playingDryMs = 0.0;

    // Character: in the path at once if it's on (no fade from a reset).
    characterFade.reset();
    characterHistory.clear();
    drive.snapToTarget();
    quality.snapToTarget();
    activeCharacter = character.getChoiceIndex();
    fadingPath = -1;
    activePath = activeCharacter == (int) Character::clean ? -1 : 0;

    for (auto& path : characterPaths)
    {
        path.reset();
        path.oversampler.resetTo (qualityNow());
    }

    if (activePath >= 0)
        characterPaths[0].curve = activeCharacter == (int) Character::warm ? dsp::Saturation::Curve::warm : dsp::Saturation::Curve::soft;

    characterLatency.store (activePath >= 0 ? characterPaths[0].oversampler.getLatencySamples() : -1, std::memory_order_relaxed);

    for (int s = 0; s < numSlots; ++s)
    {
        liveMove[(size_t) s].store (0.0f, std::memory_order_relaxed);
        liveAuto[(size_t) s].store (-std::numeric_limits<float>::infinity(), std::memory_order_relaxed);
        liveGain[(size_t) s].store (isScaled (slots[(size_t) s].shape) ? slots[(size_t) s].p.gain->getValue() * (float) scale * 0.01f : 0.0f,
                                    std::memory_order_relaxed);
    }

    gainScale.snapToTarget();

    // The output tools at their values, the auto gain worked out afresh.
    inputLoudness.reset();
    bandsLoudness.reset();
    smartCorrectionDb = 0.0;
    lastAutoGainMode = (AutoGain) autoGain.getChoiceIndex();
    curveDb = autoGain.getChoiceIndex() != (int) AutoGain::off ? curveNow() : 0.0;
    curveDirty = false;
    curveCountdown = 0;
    const auto autoDb = autoGain.getChoiceIndex() == (int) AutoGain::off ? 0.0f : (float) curveDb;
    autoGainDb.setCurrentAndTargetValue (autoDb);
    autoGainNow.store (autoDb, std::memory_order_relaxed);
    output.snapToTarget();
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output.getValue()));
    panLeft.setCurrentAndTargetValue (juce::jmin (1.0f, 1.0f - pan.getValue() * 0.01f));
    panRight.setCurrentAndTargetValue (juce::jmin (1.0f, 1.0f + pan.getValue() * 0.01f));
    widthAmount.setCurrentAndTargetValue (width.getValue() * 0.01f);
    polarity.setCurrentAndTargetValue (invert.getChoiceIndex() == 1 ? -1.0f : 1.0f);
}

double StudioEqBlock::curveGainDb (std::span<const dsp::FilterBandDesign> designs, double rate) noexcept
{
    // Pink noise has equal power per octave: the mean of |H|^2 over
    // log-spaced points is the power ratio it sees.
    auto sum = 0.0;

    for (int i = 0; i < curvePoints; ++i)
    {
        const auto f = curveLowHz * std::pow (curveHighHz / curveLowHz, i / (curvePoints - 1.0));
        sum += std::pow (10.0, dsp::responseDb (designs, f, rate) / 10.0);
    }

    return -10.0 * std::log10 (std::max (1.0e-30, sum / curvePoints));
}

double StudioEqBlock::curveNow() const noexcept
{
    // As curveGainDb, from the designs the bands are playing or gliding to,
    // with |b0 + b1 z + b2 z^2|^2 = b0^2 + b1^2 + b2^2 + 2 (b0 b1 + b1 b2) cos w + 2 b0 b2 cos 2w.
    std::array<double, curvePoints> power;
    power.fill (1.0);

    const auto scale = (double) gainScale.currentValue();

    for (const auto& slot : slots)
    {
        // Auto gain follows a band's set Gain, not its dynamic movement (Adrian,
        // 28 Nov: it mustn't push the level back up each time a band acts).
        const auto moved = slot.playing && slot.designedMoveDb != 0.0;
        const auto staticDesign = moved ? designNow (slot, scale) : dsp::FilterBandDesign {};
        const auto& design = moved ? staticDesign : slot.band.getDesign();

        for (int s = 0; s < design.numSections; ++s)
        {
            const auto& d = design.sections[(size_t) s];
            const auto n0 = d.b0 * d.b0 + d.b1 * d.b1 + d.b2 * d.b2, n1 = 2.0 * (d.b0 * d.b1 + d.b1 * d.b2), n2 = 2.0 * d.b0 * d.b2;
            const auto m0 = 1.0 + d.a1 * d.a1 + d.a2 * d.a2, m1 = 2.0 * (d.a1 + d.a1 * d.a2), m2 = 2.0 * d.a2;

            for (int i = 0; i < curvePoints; ++i)
            {
                const auto c1 = pointCos[(size_t) i], c2 = pointCos2[(size_t) i];
                power[(size_t) i] *= (n0 + n1 * c1 + n2 * c2) / std::max (1.0e-30, m0 + m1 * c1 + m2 * c2);
            }
        }
    }

    auto sum = 0.0;

    for (auto p : power)
        sum += p;

    return -10.0 * std::log10 (std::max (1.0e-30, sum / curvePoints));
}

void StudioEqBlock::processOutput (float* const* channels, int numChannels, int numSamples, bool tunerOpen) noexcept
{
    const auto mode = (AutoGain) autoGain.getChoiceIndex();

    // Curve: recomputed every 10 ms while the bands move, else nothing.
    if (mode != AutoGain::off)
    {
        curveCountdown -= numSamples;

        if (curveDirty && curveCountdown <= 0)
        {
            curveDb = curveNow();
            curveDirty = false;
            curveCountdown = curveIntervalSamples;
        }
    }

    // Smart: the bands' output measured after them (the input was measured
    // before them); a closed bin moves the correction at most 0.1 dB.
    if (mode == AutoGain::smart && bandsLoudness.process (channels, numChannels, numSamples))
    {
        const auto in = inputLoudness.getLufs(), bands = bandsLoudness.getLufs();

        // Someone is playing now (the last 100 ms), not just within the window:
        // a window that still holds the last notes must not move it in silence.
        const auto playingNow = inputLoudness.getLastBinLufs() > smartGateLufs;

        if (! tunerOpen && playingNow && in > smartGateLufs && bands > dsp::SlidingLoudness::silenceLufs)
        {
            const auto wanted = juce::jlimit (-smartLimitDb, smartLimitDb, in - bands - curveDb);
            const auto step = smartRateDbPerSecond * 0.1;
            smartCorrectionDb += juce::jlimit (-step, step, wanted - smartCorrectionDb);
        }
    }

    const auto autoTarget = mode == AutoGain::off ? 0.0 : mode == AutoGain::curve ? curveDb : curveDb + smartCorrectionDb;
    autoGainDb.setTargetValue ((float) autoTarget);
    autoGainNow.store (autoGainDb.getTargetValue(), std::memory_order_relaxed);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (output.getValue()));
    panLeft.setTargetValue (juce::jmin (1.0f, 1.0f - pan.getValue() * 0.01f));    // balance: turning right lowers the left
    panRight.setTargetValue (juce::jmin (1.0f, 1.0f + pan.getValue() * 0.01f));
    widthAmount.setTargetValue (width.getValue() * 0.01f);
    polarity.setTargetValue (invert.getChoiceIndex() == 1 ? -1.0f : 1.0f);

    const auto gainMoving = autoGainDb.isSmoothing() || outputGain.isSmoothing() || polarity.isSmoothing();
    const auto stereo = numChannels > 1;
    const auto stereoMoving = stereo && (panLeft.isSmoothing() || panRight.isSmoothing() || widthAmount.isSmoothing());
    const auto gainNow = juce::Decibels::decibelsToGain (autoGainDb.getCurrentValue()) * outputGain.getCurrentValue() * polarity.getCurrentValue();
    const auto stereoNeutral = ! stereo || (panLeft.getCurrentValue() == 1.0f && panRight.getCurrentValue() == 1.0f
                                            && widthAmount.getCurrentValue() == 1.0f);

    if (! gainMoving && ! stereoMoving && gainNow == 1.0f && stereoNeutral)
        return;   // nothing to do: bit-exact

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gainMoving ? juce::Decibels::decibelsToGain (autoGainDb.getNextValue()) * outputGain.getNextValue() * polarity.getNextValue()
                                  : gainNow;

        if (! stereo)
        {
            channels[0][i] *= g;
            continue;
        }

        // Width (mid/side), then the balance.
        const auto w = stereoMoving ? widthAmount.getNextValue() : widthAmount.getCurrentValue();
        const auto l = stereoMoving ? panLeft.getNextValue() : panLeft.getCurrentValue();
        const auto r = stereoMoving ? panRight.getNextValue() : panRight.getCurrentValue();
        const auto mid = 0.5f * (channels[0][i] + channels[1][i]);
        const auto side = 0.5f * (channels[0][i] - channels[1][i]) * w;
        channels[0][i] = (mid + side) * l * g;
        channels[1][i] = (mid - side) * r * g;
    }
}

dsp::FilterBandDesign StudioEqBlock::designNow (const Slot& slot, double scale, double moveDb) const noexcept
{
    BandSettings b;
    b.shape = (Shape) slot.shape;
    b.frequency = slot.freqRamp.getCurrentValue();
    b.gainDb = slot.p.gain->currentValue() + moveDb;   // gain scale scales both (filterSettingsFor)
    b.q = slot.qRamp.getCurrentValue();
    b.slope = slot.slope;
    return dsp::FilterBandDesign::make (filterSettingsFor (b, scale), sampleRate);
}

void StudioEqBlock::applyChoices (Slot& slot, double scale) noexcept
{
    const auto playing = slot.p.used->getChoiceIndex() == 1 && slot.p.on->getChoiceIndex() == 1;
    const auto shape = slot.p.shape->getChoiceIndex();
    const auto slope = slot.p.slope->getChoiceIndex();
    const auto place = stereoNow ? slot.p.place->getChoiceIndex() : (int) Place::stereo;   // on mono: ignored

    if (playing != slot.playing)
    {
        // On, off, in or out of use: glide to or from flat (no click).
        slot.playing = playing;
        slot.shape = shape;
        slot.slope = slope;
        slot.place = slot.oldPlace = place;

        if (playing)
            slot.snapValues();   // values set while it was off: start there

        slot.fade.reset();
        if (! playing)
            slot.moveDb = 0.0;   // a band coming back starts at rest

        slot.designedMoveDb = slot.moveDb;
        slot.band.glideTo (playing ? designNow (slot, scale, slot.moveDb) : dsp::FilterBandDesign {}, toggleSamples);
        slot.toggleRemaining = toggleSamples;
        return;
    }

    if (! playing && ! slot.band.isGliding())
        slot.place = slot.oldPlace = place;   // resting: nothing to crossfade

    // A Shape, Slope or Place change: this band only crossfades from its old
    // setting (one at a time; a change during a fade waits for it to finish).
    if (playing && (shape != slot.shape || slope != slot.slope || place != slot.place) && ! slot.fade.isActive())
    {
        slot.oldBand = slot.band;
        slot.oldPlace = slot.place;
        slot.shape = shape;
        slot.slope = slope;
        slot.place = place;
        slot.band.reset();
        slot.designedMoveDb = slot.moveDb;
        slot.band.setDesign (designNow (slot, scale, slot.moveDb));
        slot.fade.start();
    }
}

void StudioEqBlock::processSlot (Slot& slot, float* const* channels, int numChannels, int numSamples) noexcept
{
    if (slot.fade.isActive())
    {
        slot.fade.copyInput (channels, numChannels, numSamples);
        processPlaced (slot.oldBand, (Place) slot.oldPlace, slot.fade.oldChannels(), numChannels, numSamples);
        processPlaced (slot.band, (Place) slot.place, channels, numChannels, numSamples);
        slot.fade.mix (channels, numChannels, numSamples);
        return;
    }

    processPlaced (slot.band, (Place) slot.place, channels, numChannels, numSamples);
}

void StudioEqBlock::processPlaced (dsp::FilterBand<maxChannels>& band, Place place, float* const* channels, int numChannels,
                                   int numSamples) noexcept
{
    if (numChannels < 2 || place == Place::stereo)
    {
        band.process (channels, numChannels, numSamples);
        return;
    }

    if (band.getDesign().numSections == 0 && ! band.isGliding())
        return;   // changes nothing: bit-exact, no M/S round trip

    if (place == Place::left || place == Place::right)
    {
        float* one[] = { channels[place == Place::left ? 0 : 1] };
        band.process (one, 1, numSamples);   // the other channel isn't touched
        return;
    }

    // Mid or Side: encode in place (left = M, right = S), filter one, decode.
    auto* l = channels[0];
    auto* r = channels[1];

    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = 0.5f * (l[i] + r[i]), sd = 0.5f * (l[i] - r[i]);
        l[i] = m;
        r[i] = sd;
    }

    float* one[] = { place == Place::mid ? l : r };
    band.process (one, 1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = l[i], sd = r[i];
        l[i] = m + sd;
        r[i] = m - sd;
    }
}

void StudioEqBlock::processBlock (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept
{
    numChannels = juce::jlimit (1, maxChannels, numChannels);
    const juce::ScopedNoDenormals noDenormals;
    followCpu (context.cpuTight, numSamples);

    if ((numChannels > 1) != stereoNow)
    {
        // The block's channels are fixed while it plays; this is its first
        // buffer (or a new chain): placements take effect at once, no fade.
        stereoNow = numChannels > 1;

        for (auto& slot : slots)
            slot.place = slot.oldPlace = stereoNow ? slot.p.place->getChoiceIndex() : (int) Place::stereo;
    }

    processBands (channels, numChannels, numSamples, context);
    processCharacter (channels, numChannels, numSamples);
    processOutput (channels, numChannels, numSamples, context.tunerOpen);
}

void StudioEqBlock::processBands (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept
{
    // Smart auto gain: the EQ's input, before the bands.
    const auto mode = (AutoGain) autoGain.getChoiceIndex();

    if (mode != lastAutoGainMode)
    {
        // Switching mode: the Curve value afresh, Smart from it (the applied
        // gain then ramps to the new value: a crossfade).
        curveDirty = true;
        curveCountdown = 0;

        if (mode == AutoGain::smart)
        {
            smartCorrectionDb = 0.0;
            inputLoudness.reset();
            bandsLoudness.reset();
        }

        lastAutoGainMode = mode;
    }

    if (mode == AutoGain::smart)
        inputLoudness.process (channels, numChannels, numSamples);

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
    }

    // The dynamic bands' detectors, from the EQ's input (or the dry guitar),
    // before any band plays: the movement for each chunk of this buffer.
    detectDynamics (channels, numChannels, numSamples, context);

    for (int s = 0; s < numSlots; ++s)
    {
        auto& slot = slots[(size_t) s];
        const auto valuesMove = slot.playing && (slot.valuesMoving() || (scaleMoving && isScaled (slot.shape)));

        audible[(size_t) s] = slot.playing || slot.band.isGliding() || slot.fade.isActive();
        moving[(size_t) s] = valuesMove || (slot.dynamic && slot.moveChanged);
        anyMoving = anyMoving || moving[(size_t) s];

        // The Curve value follows anything that changes the bands' settings
        // (not a dynamic band's movement).
        if (valuesMove || slot.toggleRemaining > 0 || slot.fade.isActive())
            curveDirty = true;
    }

    if (scaleMoving)
        curveDirty = true;

    if (! anyMoving)
    {
        // Nothing moves: each band plays the whole buffer (a band that changes
        // nothing has no sections, so it passes bit-exact).
        for (int s = 0; s < numSlots; ++s)
            if (audible[(size_t) s])
                processSlot (slots[(size_t) s], channels, numChannels, numSamples);

        for (auto& slot : slots)
            slot.toggleRemaining = juce::jmax (0, slot.toggleRemaining - numSamples);
    }
    else
    {
        // Something moves: in chunks, redesigning what moves at the chunk's end
        // and gliding there (no zipper noise: FilterBand::glideTo).
        for (int start = 0, chunk = 0; start < numSamples; start += updateInterval, ++chunk)
        {
            const auto len = juce::jmin (updateInterval, numSamples - start);
            float* part[maxChannels] = { channels[0] + start, channels[numChannels > 1 ? 1 : 0] + start };

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

                    const auto move = slot.dynamic ? (double) movesOf (s)[chunk] : slot.moveDb;
                    slot.designedMoveDb = move;

                    // During a glide to flat's opposite (coming in), land with it.
                    const auto glide = slot.toggleRemaining > len ? slot.toggleRemaining : len;
                    slot.band.glideTo (designNow (slot, scale, move), glide);   // the old setting of a crossfade keeps its design: it fades out
                }

                if (audible[(size_t) s])
                    processSlot (slot, part, numChannels, len);

                slot.toggleRemaining = juce::jmax (0, slot.toggleRemaining - len);
            }
        }
    }

    // For U3's moving curve.
    for (int s = 0; s < numSlots; ++s)
    {
        const auto& slot = slots[(size_t) s];
        const auto scaled = slot.playing && isScaled (slot.shape);
        const auto k = (float) scale * 0.01f;
        liveGain[(size_t) s].store (scaled ? (slot.p.gain->currentValue() + (float) slot.moveDb) * k : 0.0f, std::memory_order_relaxed);
        liveMove[(size_t) s].store (scaled ? (float) slot.moveDb * k : 0.0f, std::memory_order_relaxed);
    }
}

dsp::Quality StudioEqBlock::qualityNow() const noexcept
{
    if ((getLighterSteps() & qualityLive) != 0)
        return dsp::Quality::live;   // When CPU is tight: lighter

    return (dsp::Quality) juce::jlimit (0, 2, quality.getChoiceIndex());
}

void StudioEqBlock::followCpu (bool cpuIsTight, int numSamples) noexcept
{
    if (backToFullRequested.exchange (false, std::memory_order_relaxed))
    {
        lighterSteps.store (0, std::memory_order_relaxed);   // each setting crossfades back as it plays
        lighterWait = 0;
    }

    lighterWait = juce::jmax (0, lighterWait - numSamples);

    if (! cpuIsTight || lighterWait > 0 || cpuTight.getChoiceIndex() != (int) CpuTight::makeItLighter)
        return;

    // The next step that changes something now.
    const auto taken = getLighterSteps();
    auto spectralInUse = false;

    for (const auto& slot : slots)
        spectralInUse = spectralInUse || (slot.playing && slot.p.mode->getChoiceIndex() == (int) Mode::spectral);

    auto step = 0;

    if ((taken & spectralAsDynamic) == 0 && spectralBandsBuilt && spectralInUse)
        step = spectralAsDynamic;
    else if ((taken & linearAsZeroLatency) == 0 && linearPhaseBuilt && phase.getChoiceIndex() == (int) Phase::linear)
        step = linearAsZeroLatency;
    else if ((taken & qualityLive) == 0 && character.getChoiceIndex() != (int) Character::clean
             && quality.getChoiceIndex() != (int) Quality::live)
        step = qualityLive;

    if (step == 0)
        return;   // nothing left to lighten: not marked

    lighterSteps.store (taken | step, std::memory_order_relaxed);
    lighterWait = lighterWaitSamples;   // a second to see whether that was enough
}

void StudioEqBlock::processCharacter (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto driveFrom = drive.currentValue();
    const auto driveTo = drive.skip (numSamples);
    const auto q = qualityNow();

    for (auto& path : characterPaths)
        path.oversampler.setQuality (q);   // a change crossfades 10 ms (the Oversampler)

    const auto want = character.getChoiceIndex();

    if (want != activeCharacter && ! characterFade.isActive())
    {
        // Into, out of the path, or Soft <-> Warm: the new setting comes in on
        // the free path, settled from the recent input, and crossfades 30 ms.
        fadingPath = activePath;
        activeCharacter = want;
        activePath = want == (int) Character::clean ? -1 : (fadingPath == 0 ? 1 : 0);

        if (activePath >= 0)
        {
            auto& path = characterPaths[(size_t) activePath];
            path.curve = want == (int) Character::warm ? dsp::Saturation::Curve::warm : dsp::Saturation::Curve::soft;
            warmUpCharacter (path, numChannels);
        }

        characterFade.start();
    }

    const auto fading = characterFade.isActive();
    rememberCharacterInput (channels, numChannels, numSamples);

    if (! fading && activePath < 0)
    {
        characterLatency.store (-1, std::memory_order_relaxed);
        return;   // Clean: not in the path, bit-exact
    }

    if (fading)
    {
        characterFade.copyInput (channels, numChannels, numSamples);

        if (fadingPath >= 0)
            runCharacter (characterPaths[(size_t) fadingPath], characterFade.oldChannels(), numChannels, numSamples, driveFrom, driveTo);
    }

    if (activePath >= 0)
        runCharacter (characterPaths[(size_t) activePath], channels, numChannels, numSamples, driveFrom, driveTo);

    if (fading)
        characterFade.mix (channels, numChannels, numSamples);

    // The latency now playing (a Quality change switches it as its fade starts).
    characterLatency.store (activePath >= 0 ? characterPaths[(size_t) activePath].oversampler.getLatencySamples() : -1,
                            std::memory_order_relaxed);
}

void StudioEqBlock::runCharacter (CharacterPath& path, float* const* channels, int numChannels, int numSamples, float driveFrom,
                                  float driveTo) noexcept
{
    const auto warm = path.curve == dsp::Saturation::Curve::warm;

    if (warm)
        path.pre.process (channels, numChannels, numSamples);

    // Drive into the curve, the same taken off after it; a moving Drive
    // glides across the buffer.
    const auto from = juce::Decibels::decibelsToGain (driveFrom), to = juce::Decibels::decibelsToGain (driveTo);
    const auto curve = path.curve;

    path.oversampler.process (channels, numChannels, numSamples, [this, from, to, curve]
                              (float* const* up, int n, int length, double, int) noexcept
    {
        for (int ch = 0; ch < n; ++ch)
        {
            auto* x = up[ch];

            if (from == to)
            {
                const auto inverse = 1.0f / from;

                for (int i = 0; i < length; ++i)
                    x[i] = saturation.process (curve, x[i] * from) * inverse;

                continue;
            }

            for (int i = 0; i < length; ++i)
            {
                const auto g = from + (to - from) * (float) (i + 1) / (float) length;
                x[i] = saturation.process (curve, x[i] * g) / g;
            }
        }
    });

    if (warm)
        path.post.process (channels, numChannels, numSamples);

    path.dcBlocker.process (channels, numChannels, numSamples);
}

void StudioEqBlock::rememberCharacterInput (const float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto keep = characterHistory.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* h = characterHistory.getWritePointer (ch);

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

void StudioEqBlock::warmUpCharacter (CharacterPath& path, int numChannels) noexcept
{
    // The stage comes in settled from the recent input, not from silence
    // (which would click as it fades in).
    path.reset();
    const auto d = drive.currentValue();
    const auto chunk = characterWarmup.getNumSamples();

    for (int start = 0; start < characterWarmupSamples; start += chunk)
    {
        const auto length = juce::jmin (chunk, characterWarmupSamples - start);
        float* part[maxChannels] {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            characterWarmup.copyFrom (ch, 0, characterHistory, ch, start, length);
            part[ch] = characterWarmup.getWritePointer (ch);
        }

        runCharacter (path, part, numChannels, length, d, d);
    }
}

void StudioEqBlock::detectDynamics (const float* const* channels, int numChannels, int numSamples,
                                    const ProcessContext& context) noexcept
{
    // Which bands run their dynamics this buffer: a playing band with a gain
    // in Dynamic (or Spectral) mode with a Range, or one still on its way
    // back to rest. A band at rest with Range 0 costs nothing (bit-exact static).
    auto any = false;

    for (auto& slot : slots)
    {
        slot.moveChanged = false;
        const auto wanted = slot.playing && canBeDynamic ((Shape) slot.shape)
                            && playedMode ((Mode) slot.p.mode->getChoiceIndex()) == Mode::dynamic;
        slot.dynamic = slot.playing && ((wanted && slot.p.range->getValue() != 0.0f) || slot.moveDb != 0.0);
        any = any || slot.dynamic;

        if (! slot.playing)
            slot.moveDb = 0.0;
    }

    if (! any)
        return;

    // The EQ's input (This band's sidechain: the bands haven't played yet).
    for (int ch = 0; ch < numChannels; ++ch)
        eqInput.copyFrom (ch, 0, channels[ch], numSamples);

    // Someone is playing, per chunk, on the input and on the dry guitar: Auto
    // learns only then (and with the tuner closed).
    const auto* dry = context.chainInput;
    const auto playing = [this, numSamples] (const float* const* source, int numSource, double& ms, std::vector<std::uint8_t>& out)
    {
        for (int start = 0, c = 0; start < numSamples; start += updateInterval, ++c)
        {
            const auto len = juce::jmin (updateInterval, numSamples - start);
            auto loudest = 0.0;

            for (int k = 0; k < numSource; ++k)
            {
                auto sum = 0.0;

                for (int i = 0; i < len; ++i)
                    sum += (double) source[k][start + i] * (double) source[k][start + i];

                loudest = std::max (loudest, sum / len);
            }

            // Playing: the last 50 ms above the gate, and this chunk not
            // silent (so the first moments of silence don't count either).
            ms += (loudest - ms) * (1.0 - std::exp (-1000.0 * len / (playingMs * sampleRate)));
            out[(size_t) c] = 10.0 * std::log10 (ms + 1.0e-30) > playingDb
                                  && 10.0 * std::log10 (loudest + 1.0e-30) > playingDb - playingChunkBelowDb ? 1 : 0;
        }
    };

    const float* input[maxChannels] = { eqInput.getReadPointer (0), eqInput.getReadPointer (juce::jmin (1, numChannels - 1)) };
    playing (input, numChannels, playingInputMs, playingInput);

    // Mid and Side bands hear M or S of the input.
    auto midSide = false;

    for (const auto& slot : slots)
        midSide = midSide || (slot.dynamic && (slot.place == (int) Place::mid || slot.place == (int) Place::side));

    if (midSide && numChannels > 1)
    {
        auto* m = eqInputMidSide.getWritePointer (0);
        auto* sd = eqInputMidSide.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            m[i] = 0.5f * (input[0][i] + input[1][i]);
            sd[i] = 0.5f * (input[0][i] - input[1][i]);
        }
    }

    if (dry != nullptr)
        playing (&dry, 1, playingDryMs, playingDry);

    for (int s = 0; s < numSlots; ++s)
    {
        auto& slot = slots[(size_t) s];

        if (! slot.dynamic)
            continue;

        // The detector follows the placement: Stereo the louder channel, Left
        // or Right that channel, Mid or Side M or S; the dry guitar is mono.
        if (slot.p.side->getChoiceIndex() == (int) Side::dryGuitar && dry != nullptr)
        {
            detectSlot (slot, s, &dry, 1, numSamples, playingDry.data(), context.tunerOpen);
            continue;
        }

        const float* one[1] = { input[0] };

        switch ((Place) slot.place)
        {
            case Place::left:  one[0] = input[0]; break;
            case Place::right: one[0] = input[1]; break;
            case Place::mid:   one[0] = eqInputMidSide.getReadPointer (0); break;
            case Place::side:  one[0] = eqInputMidSide.getReadPointer (1); break;
            case Place::stereo:
            default:
                detectSlot (slot, s, input, numChannels, numSamples, playingInput.data(), context.tunerOpen);
                continue;
        }

        detectSlot (slot, s, one, 1, numSamples, playingInput.data(), context.tunerOpen);
    }
}

void StudioEqBlock::detectSlot (Slot& slot, int index, const float* const* source, int numSource, int numSamples,
                                const std::uint8_t* playing, bool tunerOpen) noexcept
{
    const auto wanted = canBeDynamic ((Shape) slot.shape) && playedMode ((Mode) slot.p.mode->getChoiceIndex()) == Mode::dynamic;
    const auto range = wanted ? (double) slot.p.range->getValue() : 0.0;
    slot.p.range->snapToTarget();
    auto* out = movesOf (index);

    // The sidechain filter, from the band's settings now (Follows) or its own.
    if (range != 0.0)
    {
        auto band = getBand (index);
        band.shape = (Shape) slot.shape;
        auto filtered = false;
        const auto settings = sidechainSettingsFor (band, (SideFilter) slot.p.sideFilter->getChoiceIndex(),
                                                    slot.p.sideFreq->getValue(), slot.p.sideQ->getValue(), filtered);

        if (! slot.sideDesigned || filtered != slot.sideFiltered || ! (settings == slot.sideSettings))
        {
            slot.sideBand.setDesign (filtered ? dsp::FilterBandDesign::make (settings, sampleRate) : dsp::FilterBandDesign {});
            slot.sideSettings = settings;
            slot.sideFiltered = filtered;
            slot.sideDesigned = true;
        }
    }

    const auto autoThreshold = slot.p.thresh->getValue() < thresholdAutoBelowDb;
    const auto threshold = (double) slot.p.thresh->getValue();
    const auto attackSamples = (double) slot.p.attack->getValue() * 0.001 * sampleRate;
    const auto releaseSamples = (double) slot.p.release->getValue() * 0.001 * sampleRate;
    const auto autoSamples = autoWindowSeconds * sampleRate;
    numSource = juce::jlimit (1, maxChannels, numSource);

    auto m = slot.moveDb;

    for (int start = 0, c = 0; start < numSamples; start += updateInterval, ++c)
    {
        const auto len = juce::jmin (updateInterval, numSamples - start);
        auto target = 0.0;

        if (range != 0.0)
        {
            // RMS of the filtered sidechain; Stereo hears the louder channel.
            auto level = 0.0, chunkMs = 0.0;

            for (int k = 0; k < numSource; ++k)
            {
                auto ms = slot.sideMs[(size_t) k], sum = 0.0;
                const auto* x = source[k] + start;

                for (int i = 0; i < len; ++i)
                {
                    const auto y = (double) slot.sideBand.processSample (x[i], k);
                    const auto y2 = y * y;
                    ms = y2 + (ms - y2) * detectorCoefficient;
                    sum += y2;
                }

                slot.sideMs[(size_t) k] = ms;
                level = std::max (level, ms);
                chunkMs = std::max (chunkMs, sum / len);
            }

            auto thresholdDb = threshold;

            if (autoThreshold)
            {
                // A slow RMS of what comes in, only while someone plays and the
                // tuner is closed; frozen otherwise. Before it has heard anything
                // the band doesn't move.
                if (playing[c] != 0 && ! tunerOpen)
                {
                    if (! slot.autoLearned)
                    {
                        slot.autoMs = std::max (level, chunkMs);
                        slot.autoLearned = slot.autoMs > 0.0;
                    }
                    else
                    {
                        slot.autoMs += (chunkMs - slot.autoMs) * (1.0 - std::exp (-len / autoSamples));
                    }
                }

                thresholdDb = slot.autoLearned ? 10.0 * std::log10 (slot.autoMs + 1.0e-30) + autoAboveDb
                                               : std::numeric_limits<double>::infinity();
            }

            if (std::isfinite (thresholdDb))
                target = movementDb (10.0 * std::log10 (level + 1.0e-30) - thresholdDb, range);
        }

        // Attack as the movement grows, Release as it goes back (in dB).
        const auto samples = std::abs (target) > std::abs (m) ? attackSamples : releaseSamples;
        m = target + (m - target) * std::exp (-len / std::max (1.0, samples));

        if (std::abs (m - target) < settledDb)
            m = target;

        const auto before = c == 0 ? slot.designedMoveDb : (double) out[c - 1];
        out[c] = (float) m;
        m = (double) out[c];   // the value the band is designed with
        slot.moveChanged = slot.moveChanged || m != before;
    }

    slot.moveDb = m;

    if (autoThreshold)
        liveAuto[(size_t) index].store (slot.autoLearned ? (float) (10.0 * std::log10 (slot.autoMs + 1.0e-30) + autoAboveDb)
                                                         : -std::numeric_limits<float>::infinity(),
                                        std::memory_order_relaxed);
}

//==============================================================================
namespace
{
    // Factory presets (EQ-PLAN §4, values Proposed, tuned by ear on the rig;
    // ids stable once merged). The bands go into slots 01, 02 ... A preset is
    // the whole sound: every other preset parameter goes to its default, so
    // slots it doesn't use are freed. "Tame fizz" and "Pick attack" need
    // dynamic bands and come with B4.
    constexpr float lowCut = (float) StudioEqBlock::Shape::lowCut, highCut = (float) StudioEqBlock::Shape::highCut;
    constexpr float lowShelf = (float) StudioEqBlock::Shape::lowShelf, bell = (float) StudioEqBlock::Shape::bell;
    constexpr float slope12 = 1.0f, slope18 = 2.0f, slope24 = 3.0f;

    constexpr PresetValue tightenLows[] {
        { "b01Used", 1.0f }, { "b01Shape", lowCut }, { "b01Freq", 90.0f }, { "b01Slope", slope24 },
        { "b02Used", 1.0f }, { "b02Shape", lowShelf }, { "b02Freq", 150.0f }, { "b02Gain", -2.0f }, { "b02Q", 0.7071f },
    };
    constexpr PresetValue cutMud[] {
        { "b01Used", 1.0f }, { "b01Shape", bell }, { "b01Freq", 300.0f }, { "b01Gain", -3.0f }, { "b01Q", 1.2f },
    };
    constexpr PresetValue presence[] {
        { "b01Used", 1.0f }, { "b01Shape", bell }, { "b01Freq", 3000.0f }, { "b01Gain", 2.5f }, { "b01Q", 0.9f },
    };
    constexpr PresetValue sitInMix[] {
        { "b01Used", 1.0f }, { "b01Shape", lowCut }, { "b01Freq", 100.0f }, { "b01Slope", slope18 },
        { "b02Used", 1.0f }, { "b02Shape", bell }, { "b02Freq", 250.0f }, { "b02Gain", -2.0f },
        { "b03Used", 1.0f }, { "b03Shape", bell }, { "b03Freq", 2500.0f }, { "b03Gain", 2.0f },
        { "b04Used", 1.0f }, { "b04Shape", highCut }, { "b04Freq", 9000.0f }, { "b04Slope", slope12 },
    };

    constexpr FactoryPreset studioEqPresets[] {
        { "flat", "Flat", {} },
        { "tightenLows", "Tighten the low end", tightenLows },
        { "cutMud", "Cut the mud", cutMud },
        { "presence", "Presence", presence },
        { "sitInMix", "Sit in the mix", sitInMix },
    };
}

std::span<const FactoryPreset> StudioEqBlock::getFactoryPresets() const noexcept
{
    return studioEqPresets;
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
