// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/StudioEqBlock.h"

#include "engine/core/FactoryPresets.h"

#include <cmath>
#include <cstring>
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
    subBands.resize ((size_t) numSlots);

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

    // B5, after every B4 parameter: the spectral method (setup, block-wide),
    // and per band the spectral Density and Tilt (EQ-PLAN §2.2).
    spectralMethod = &addParameter (choice ("spectralMethod", "Spectral method", { "Sub-bands", "FFT" },
                                            (int) SpectralMethod::subBands, false));

    for (int s = 0; s < numSlots; ++s)
    {
        auto& p = slots[(size_t) s].p;
        const auto label = [s] (const char* what) { return "Band " + juce::String (s + 1) + " " + what; };
        p.density  = &addParameter (number (slotId (s, "Density"), label ("spectral density"), { 0.0f, 100.0f }, defaultDensity, "%", 0));
        p.specTilt = &addParameter (choice (slotId (s, "SpecTilt"), label ("spectral tilt"), { "Off", "On" }, 1, false));
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

StudioEqBlock::Mode StudioEqBlock::modeOf (int index) const noexcept
{
    const auto& slot = slots[(size_t) index];

    if (! canBeDynamic ((Shape) slot.shape))
        return Mode::staticBand;

    const auto mode = (Mode) slot.p.mode->getChoiceIndex();

    if (mode == Mode::spectral && (getLighterSteps() & spectralAsDynamic) != 0)
        return Mode::dynamic;   // When CPU is tight: lighter

    return mode;
}

int StudioEqBlock::getNumSubBands (int slot) const noexcept
{
    return subBands[(size_t) slot].count;
}

double StudioEqBlock::getSubBandFrequency (int slot, int subBand) const noexcept
{
    return subBands[(size_t) slot].subs[(size_t) subBand].frequency;
}

float StudioEqBlock::getSubBandGainDb (int slot, int subBand) const noexcept
{
    return (float) (subBands[(size_t) slot].subs[(size_t) subBand].moveDb * gainScale.currentValue() * 0.01);
}

dsp::FilterBandDesign StudioEqBlock::subBandDesign (const SubBand& sub, double moveDb, double scale) const noexcept
{
    const auto gain = moveDb * scale * 0.01;

    if (gain == 0.0)
        return {};

    return dsp::FilterBandDesign::make ({ dsp::FilterShape::bell, sub.frequency, gain, sub.q, 12 }, sampleRate);
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

dsp::FilterSettings StudioEqBlock::listenSettingsFor (const BandSettings& band, bool& filtered) noexcept
{
    // As a dynamic band's Follows filter, and the cuts by the side they act on.
    auto b = band;

    if (band.shape == Shape::lowCut)
        b.shape = Shape::lowShelf;
    else if (band.shape == Shape::highCut)
        b.shape = Shape::highShelf;
    else if (band.shape == Shape::notch || band.shape == Shape::bandPass || band.shape == Shape::allPass)
        b.shape = Shape::bell;

    return sidechainSettingsFor (b, SideFilter::follows, 0.0, 0.0, filtered);
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

    // Linear phase: two engines, so one can warm up while the other plays.
    for (auto& engine : engines)
        engine.prepare (sampleRate, maxChannels);

    pathScratch.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    dipSamples = juce::jmax (1, juce::roundToInt (pathDipSeconds * sampleRate));

    // Listen.
    listenBuffer.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    listenAmount.reset (sampleRate, listenFadeSeconds);

    // Dynamics: the EQ's input, the movement per chunk, per slot.
    eqInput.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    eqInputMidSide.setSize (maxChannels, juce::jmax (1, spec.maxBlockSize), false, true, false);
    maxChunks = juce::jmax (1, (spec.maxBlockSize + updateInterval - 1) / updateInterval);
    moves.assign ((size_t) numSlots * (size_t) maxChunks, 0.0f);
    subMoves.assign ((size_t) numSlots * (size_t) maxSubBands * (size_t) maxChunks, 0.0f);

    // FFT spectral bands: the detectors and the bin movements, for High at this rate.
    const auto maxN = dsp::LinearPhaseFilter::baseLength[2] * dsp::LinearPhaseFilter::rateFactor (sampleRate);
    maxBins = maxN / 2 + 1;
    binMoves.assign ((size_t) numSlots * (size_t) maxBins, 0.0f);
    binMoveSize.assign ((size_t) numSlots, 0);
    binTarget.assign ((size_t) maxBins, 0.0f);

    for (auto& detector : spectralDetectors)
        detector.prepare (maxN);
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
        slot.spectral = slot.subMoveChanged = false;
        slot.dynamic = slot.moveChanged = false;
    }

    playingInputMs = playingDryMs = 0.0;

    // FFT spectral bands at rest.
    std::fill (binMoves.begin(), binMoves.end(), 0.0f);
    std::fill (binMoveSize.begin(), binMoveSize.end(), 0);

    for (auto& detector : spectralDetectors)
        detector.reset();

    for (auto& slot : slots)
        slot.fftSpectral = slot.fftChanged = false;

    // Spectral sub-bands at rest, laid out afresh.
    for (auto& sb : subBands)
    {
        for (auto& sub : sb.subs)
        {
            sub.detector.reset();
            sub.band.reset();
            sub.ms.fill (0.0);
            sub.band.setDesign ({});
            sub.ms.fill (0.0);
            sub.moveDb = sub.designedMoveDb = 0.0;
        }

        sb.count = 0;
        sb.layoutKey = { -1.0 };

        for (auto& g : sb.guards)
            g.reset();

        for (auto& g : sb.guardMs)
            g.fill (0.0);
    }

    // Linear phase: the wanted path at once (nothing is playing yet).
    startPath (wantedPath (stereoNow ? 2 : 1), stereoNow ? 2 : 1, true);

    // Listen: whatever is asked now, at once.
    listenSlot = listenRequest.load (std::memory_order_relaxed);
    listenAmount.setCurrentAndTargetValue (listenSlot >= 0 ? 1.0f : 0.0f);
    listenBand.reset();
    listenFiltered = false;
    listenBand.setDesign ({});

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
    if (! iirAudible)
    {
        // Linear phase plays: the bands' designs move on, their filters rest
        // (warmed up again before they're heard: warmIirBands).
        slot.fade.reset();
        return;
    }

    if (slot.fade.isActive())
    {
        slot.fade.copyInput (channels, numChannels, numSamples);
        processPlaced (slot.oldBand, (Place) slot.oldPlace, slot.fade.oldChannels(), numChannels, numSamples);
        processPlaced (slot.band, (Place) slot.place, channels, numChannels, numSamples);
        slot.fade.mix (channels, numChannels, numSamples);
    }
    else
    {
        processPlaced (slot.band, (Place) slot.place, channels, numChannels, numSamples);
    }

    // A spectral band's sub-bands, in the band's placement: only those that move.
    auto& sb = subBands[(size_t) (&slot - slots.data())];

    for (int k = 0; k < sb.count; ++k)
    {
        auto& sub = sb.subs[(size_t) k];

        if (sub.band.getDesign().numSections > 0 || sub.band.isGliding())
            processPlaced (sub.band, (Place) slot.place, channels, numChannels, numSamples);
    }
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

        startPath (wantedPath (numChannels), numChannels, true);   // and the path, at once
    }

    eqInputCopied = false;

    if (const auto wanted = wantedPath (numChannels); pathState == PathState::steady && ! (wanted == activeConfig))
        startPath (wanted, numChannels, false);

    if (listenSlot >= 0 || listenRequest.load (std::memory_order_relaxed) >= 0 || pathState != PathState::steady)
        copyEqInput (channels, numChannels, numSamples);   // listen and a path warming up play from the EQ's input

    processPaths (channels, numChannels, numSamples, context);
    processCharacter (channels, numChannels, numSamples);
    processOutput (channels, numChannels, numSamples, context.tunerOpen);
    processListen (channels, numChannels, numSamples, context);
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
        moving[(size_t) s] = valuesMove || (slot.dynamic && slot.moveChanged) || (slot.spectral && slot.subMoveChanged);
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

                    // A spectral band's sub-bands: the ones that move (or a new layout, or gain scale).
                    auto& sb = subBands[(size_t) s];

                    for (int k = 0; k < sb.count; ++k)
                    {
                        auto& sub = sb.subs[(size_t) k];
                        const auto m = slot.spectral ? (double) subMovesOf (s, k)[chunk] : sub.moveDb;

                        if (! (m == sub.designedMoveDb) || (scaleMoving && m != 0.0))
                        {
                            sub.band.glideTo (subBandDesign (sub, m, scale), len);
                            sub.designedMoveDb = m;
                        }
                    }
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

StudioEqBlock::PathConfig StudioEqBlock::wantedPath (int numChannels) const noexcept
{
    PathConfig config;
    config.resolution = (dsp::LinearPhaseFilter::Resolution) juce::jlimit (0, 2, linearQuality.getChoiceIndex());
    config.linear = phase.getChoiceIndex() == (int) Phase::linear && (getLighterSteps() & linearAsZeroLatency) == 0;

    // Zero latency (and Natural, until R1): the IIR bands, with a linear-phase
    // stage after them only for FFT spectral bands.
    auto lr = false, ms = false;

    for (int s = 0; s < numSlots; ++s)
    {
        const auto& slot = slots[(size_t) s];

        if (! slot.playing || (! config.linear && ! isFftSpectral (s)))
            continue;

        config.spectralStage = config.spectralStage || ! config.linear;

        if (numChannels > 1 && (slot.place == (int) Place::mid || slot.place == (int) Place::side))
            ms = true;
        else
            lr = true;
    }

    if (! config.linear && ! config.spectralStage)
        return PathConfig {};   // the IIR bands alone: nothing else matters

    // The L/R stage for Stereo, Left and Right bands, the M/S stage for Mid
    // and Side ones; both when both are in use.
    config.stages = ms ? (lr ? dsp::LinearPhaseFilter::Stages::both : dsp::LinearPhaseFilter::Stages::ms)
                       : dsp::LinearPhaseFilter::Stages::lr;
    return config;
}

void StudioEqBlock::warmIirBands() noexcept
{
    // The IIR bands rested while Linear phase played: their designs are
    // current, their filters start clean (heard only after the dip's first half).
    for (auto& slot : slots)
    {
        slot.band.reset();
        const auto design = slot.band.getDesign();
        slot.band.setDesign (design);
        slot.fade.reset();
        slot.toggleRemaining = 0;
    }
}

void StudioEqBlock::syncEngine (dsp::LinearPhaseFilter& engine, int numChannels, bool stageOnly) noexcept
{
    // Full: every band (Linear phase). Stage only: just the FFT spectral
    // bands' bin movements (the stage after the IIR bands in Zero latency).
    using Lp = dsp::LinearPhaseFilter;
    const auto scale = gainScale.currentValue() * 0.01f;

    for (int s = 0; s < numSlots; ++s)
    {
        const auto& slot = slots[(size_t) s];
        auto& es = engine.getSlot (s);
        const auto fft = slot.fftSpectral && binMoveSize[(size_t) s] == engine.getNumBins();
        const auto active = stageOnly ? (slot.playing && isFftSpectral (s)) : slot.playing;
        auto target = Lp::Target::both;

        if (numChannels > 1)
        {
            switch ((Place) slot.place)
            {
                case Place::left:  target = Lp::Target::left; break;
                case Place::right: target = Lp::Target::right; break;
                case Place::mid:   target = Lp::Target::mid; break;
                case Place::side:  target = Lp::Target::side; break;
                case Place::stereo:
                default: break;
            }
        }

        auto brickwall = Lp::Brickwall::none;

        if (! stageOnly && slot.slope == brickwallSlope && slot.shape == (int) Shape::lowCut)
            brickwall = Lp::Brickwall::lowCut;
        else if (! stageOnly && slot.slope == brickwallSlope && slot.shape == (int) Shape::highCut)
            brickwall = Lp::Brickwall::highCut;

        const auto frequency = slot.freqRamp.getCurrentValue();
        const auto& design = slot.band.getDesign();
        const auto& sb = subBands[(size_t) s];
        auto subDesigns = 0;

        for (int k = 0; k < sb.count; ++k)
            if (sb.subs[(size_t) k].band.getDesign().numSections > 0)
                ++subDesigns;

        const auto designs = stageOnly ? 0 : 1 + subDesigns;
        const auto sameDesign = stageOnly
                                || (es.numDesigns == designs && ! slot.subMoveChanged && es.designs[0].numSections == design.numSections
                                    && std::memcmp (es.designs[0].sections.data(), design.sections.data(),
                                                    sizeof (dsp::BiquadDesign) * (size_t) design.numSections) == 0);
        const auto changed = es.active != active
                             || (active && (es.target != target || es.brickwall != brickwall || es.binGains != fft
                                            || (fft && slot.fftChanged)
                                            || (brickwall != Lp::Brickwall::none ? es.brickwallFrequency != frequency : ! sameDesign)));

        if (! changed)
            continue;

        es.active = active;
        es.target = target;
        es.brickwall = brickwall;
        es.brickwallFrequency = frequency;
        es.numDesigns = 0;

        if (! stageOnly)
        {
            es.designs[0] = design;
            es.numDesigns = 1;

            for (int k = 0; k < sb.count && es.numDesigns < Lp::maxDesignsPerSlot; ++k)
                if (sb.subs[(size_t) k].band.getDesign().numSections > 0)
                    es.designs[(size_t) es.numDesigns++] = sb.subs[(size_t) k].band.getDesign();
        }

        es.binGains = fft;

        if (fft)
        {
            const auto* g = binMoves.data() + (size_t) s * (size_t) maxBins;
            auto* out = engine.getBinGains (s);

            for (int k = 0; k < engine.getNumBins(); ++k)
                out[k] = g[k] * scale;
        }

        engine.slotChanged (s);
    }
}

void StudioEqBlock::startPath (const PathConfig& config, int numChannels, bool immediately) noexcept
{
    const auto usesEngine = config.linear || config.spectralStage;
    const auto prepareEngine = [&] (int index)
    {
        auto& engine = engines[(size_t) index];
        engine.configure (config.resolution, config.stages, numChannels);
        syncEngine (engine, numChannels, ! config.linear);
        engine.reset();
    };

    if (immediately)
    {
        activeEngine = usesEngine ? 0 : -1;

        if (usesEngine)
            prepareEngine (0);

        activeConfig = targetConfig = config;
        targetEngine = -1;
        pathState = PathState::steady;
        pathLatency.store (usesEngine ? engines[0].getLatencySamples() : 0, std::memory_order_relaxed);
        linearPlaying.store (config.linear, std::memory_order_relaxed);
        pathChanging.store (false, std::memory_order_relaxed);
        iirAudible = ! config.linear;
        return;
    }

    // The new path warms up on a copy of the input while the old one plays.
    targetConfig = config;
    targetEngine = usesEngine ? (activeEngine == 0 ? 1 : 0) : -1;

    if (usesEngine)
        prepareEngine (targetEngine);

    if (! config.linear && activeConfig.linear)
        warmIirBands();   // they come back from resting (while they play, they're left alone)

    pathState = PathState::warming;
    pathChanging.store (true, std::memory_order_relaxed);
}

void StudioEqBlock::processPaths (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept
{
    const auto changing = pathState == PathState::warming || pathState == PathState::dipOut;
    const auto activeIir = ! activeConfig.linear, targetIir = changing && ! targetConfig.linear;
    float* scratch[maxChannels] = { pathScratch.getWritePointer (0), pathScratch.getWritePointer (1) };

    if (changing && ! (activeIir && targetIir))
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (eqInput.getReadPointer (ch), eqInput.getReadPointer (ch) + numSamples, scratch[ch]);

    // The IIR bands: the path while they play (both paths, when both use
    // them: the new one gets their output); on the copy while they're the
    // path coming in; otherwise their designs only (Linear phase reads them).
    if (activeIir)
    {
        iirAudible = true;
        processBands (channels, numChannels, numSamples, context);

        if (targetIir)
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (channels[ch], channels[ch] + numSamples, scratch[ch]);
    }
    else if (targetIir)
    {
        iirAudible = true;
        processBands (scratch, numChannels, numSamples, context);
    }
    else
    {
        iirAudible = false;
        processBands (channels, numChannels, numSamples, context);
    }

    if (activeEngine >= 0)
    {
        syncEngine (engines[(size_t) activeEngine], numChannels, activeIir);
        engines[(size_t) activeEngine].process (channels, numChannels, numSamples);
    }

    if (changing && targetEngine >= 0)
    {
        syncEngine (engines[(size_t) targetEngine], numChannels, targetIir);
        engines[(size_t) targetEngine].process (scratch, numChannels, numSamples);
    }

    for (auto& slot : slots)
        slot.fftChanged = false;

    if (pathState == PathState::warming && (targetEngine < 0 || engines[(size_t) targetEngine].isSettled()))
    {
        pathState = PathState::dipOut;
        dipRemaining = dipSamples;
    }

    if (pathState != PathState::dipOut && pathState != PathState::dipIn)
        return;

    // The dip: the old path out, then (from the copy, this buffer) the new one in.
    auto fromScratch = false;

    for (int i = 0; i < numSamples; ++i)
    {
        auto gain = 1.0f;

        if (pathState == PathState::dipOut)
        {
            gain = (float) dipRemaining / (float) dipSamples;

            if (--dipRemaining <= 0)
            {
                // Silence: the new path takes over, with its latency.
                activeEngine = targetEngine;
                activeConfig = targetConfig;
                targetEngine = -1;
                pathLatency.store (activeEngine >= 0 ? engines[(size_t) activeEngine].getLatencySamples() : 0,
                                   std::memory_order_relaxed);
                linearPlaying.store (activeConfig.linear, std::memory_order_relaxed);
                pathState = PathState::dipIn;
                dipRemaining = dipSamples;
                fromScratch = true;
            }
        }
        else if (pathState == PathState::dipIn)
        {
            gain = 1.0f - (float) dipRemaining / (float) dipSamples;

            if (--dipRemaining <= 0)
            {
                pathState = PathState::steady;
                pathChanging.store (false, std::memory_order_relaxed);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = (fromScratch ? scratch[ch][i] : channels[ch][i]) * gain;
    }
}

void StudioEqBlock::copyEqInput (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (eqInputCopied)
        return;

    for (int ch = 0; ch < numChannels; ++ch)
        eqInput.copyFrom (ch, 0, channels[ch], numSamples);

    eqInputCopied = true;
}

void StudioEqBlock::processListen (float* const* channels, int numChannels, int numSamples, const ProcessContext& context) noexcept
{
    auto wanted = listenRequest.load (std::memory_order_relaxed);

    if (wanted >= 0 && slots[(size_t) wanted].p.used->getChoiceIndex() != 1)
        wanted = -1;   // a free slot plays nothing

    // Another band (or none): out first, then switch, then in.
    if (wanted != listenSlot)
    {
        listenAmount.setTargetValue (0.0f);

        if (listenAmount.getCurrentValue() <= 0.0f)
        {
            listenSlot = wanted;
            listenBand.reset();
            listenFiltered = false;
            listenBand.setDesign ({});
        }
    }

    if (wanted == listenSlot)
        listenAmount.setTargetValue (listenSlot >= 0 ? 1.0f : 0.0f);

    if (listenSlot < 0 || ! eqInputCopied)
        return;   // nothing to hear (the fade out ended)

    // What the band hears: its sidechain if it's dynamic, else the EQ's input.
    const auto& slot = slots[(size_t) listenSlot];
    auto band = getBand (listenSlot);
    const auto dynamic = modeOf (listenSlot) == Mode::dynamic || modeOf (listenSlot) == Mode::spectral;
    auto filtered = false;
    const auto settings = dynamic ? sidechainSettingsFor (band, (SideFilter) slot.p.sideFilter->getChoiceIndex(),
                                                          slot.p.sideFreq->getValue(), slot.p.sideQ->getValue(), filtered)
                                  : listenSettingsFor (band, filtered);

    if (filtered != listenFiltered || ! (settings == listenSettings))
    {
        listenBand.glideTo (filtered ? dsp::FilterBandDesign::make (settings, sampleRate) : dsp::FilterBandDesign {}, numSamples);
        listenSettings = settings;
        listenFiltered = filtered;
    }

    // The source on each channel: the dry guitar, or the input as the band's
    // placement hears it (Stereo: each channel its own).
    const auto* dry = context.chainInput;
    const auto fromDry = dynamic && slot.p.side->getChoiceIndex() == (int) Side::dryGuitar && dry != nullptr;
    const auto place = numChannels > 1 && dynamic ? (Place) slot.place : Place::stereo;
    const auto* in0 = eqInput.getReadPointer (0);
    const auto* in1 = eqInput.getReadPointer (juce::jmin (1, numChannels - 1));
    auto* out0 = listenBuffer.getWritePointer (0);
    auto* out1 = listenBuffer.getWritePointer (1);
    auto perChannel = false;

    for (int i = 0; i < numSamples; ++i)
    {
        if (fromDry)
            out0[i] = dry[i];
        else if (place == Place::left)
            out0[i] = in0[i];
        else if (place == Place::right)
            out0[i] = in1[i];
        else if (place == Place::mid)
            out0[i] = 0.5f * (in0[i] + in1[i]);
        else if (place == Place::side)
            out0[i] = 0.5f * (in0[i] - in1[i]);
        else
        {
            out0[i] = in0[i];
            out1[i] = in1[i];
            perChannel = numChannels > 1;
        }
    }

    float* listen[maxChannels] = { out0, out1 };
    listenBand.process (listen, perChannel ? 2 : 1, numSamples);

    if (numChannels > 1 && ! perChannel)
        listenBuffer.copyFrom (1, 0, listenBuffer, 0, 0, numSamples);   // every channel

    // Replace the output, crossfaded.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto a = listenAmount.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] += (listenBuffer.getSample (ch, i) - channels[ch][i]) * a;
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
    else if ((taken & linearAsZeroLatency) == 0 && phase.getChoiceIndex() == (int) Phase::linear)
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
        const auto index = (int) (&slot - slots.data());
        slot.moveChanged = slot.subMoveChanged = false;
        const auto mode = modeOf (index);
        const auto wanted = slot.playing && mode == Mode::dynamic;
        slot.dynamic = slot.playing && ((wanted && slot.p.range->getValue() != 0.0f) || slot.moveDb != 0.0);

        // Spectral, sub-band method: while it has a Range, or a sub-band is on its way back.
        auto& sb = subBands[(size_t) index];
        auto subMoving = false;

        for (int k = 0; k < sb.count; ++k)
            subMoving = subMoving || sb.subs[(size_t) k].moveDb != 0.0;

        const auto wantedSpectral = slot.playing && mode == Mode::spectral
                                    && spectralMethod->getChoiceIndex() == (int) SpectralMethod::subBands;
        slot.spectral = slot.playing && ((wantedSpectral && slot.p.range->getValue() != 0.0f) || subMoving);
        slot.fftSpectral = isFftSpectral (index);
        any = any || slot.dynamic || slot.spectral || slot.fftSpectral;

        if (! slot.fftSpectral && binMoveSize[(size_t) index] != 0)
        {
            std::fill (binMovesOf (index), binMovesOf (index) + maxBins, 0.0f);   // the kernel drops it (crossfaded)
            binMoveSize[(size_t) index] = 0;
        }

        if (! slot.playing)
        {
            slot.moveDb = 0.0;

            for (int k = 0; k < sb.count; ++k)
                sb.subs[(size_t) k].moveDb = 0.0;
        }
    }

    if (! any)
        return;

    // The EQ's input (This band's sidechain: the bands haven't played yet).
    copyEqInput (channels, numChannels, numSamples);

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
        midSide = midSide || ((slot.dynamic || slot.spectral || slot.fftSpectral)
                              && (slot.place == (int) Place::mid || slot.place == (int) Place::side));

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

    // The FFT bands' detectors, by source (shared by the bands that hear the same thing).
    std::array<const float*, numSpectralSources> sourceA {}, sourceB {};
    std::array<int, numSpectralSources> sourceChannels {};

    for (int s = 0; s < numSlots; ++s)
    {
        auto& slot = slots[(size_t) s];

        if (! slot.dynamic && ! slot.spectral && ! slot.fftSpectral)
            continue;

        // The detector follows the placement: Stereo the louder channel, Left
        // or Right that channel, Mid or Side M or S; the dry guitar is mono.
        const float* one[1] = { input[0] };
        const float* const* source = one;
        auto numSource = 1;
        const auto* playingNow = playingInput.data();

        if (slot.p.side->getChoiceIndex() == (int) Side::dryGuitar && dry != nullptr)
        {
            one[0] = dry;
            playingNow = playingDry.data();
        }
        else
        {
            switch ((Place) slot.place)
            {
                case Place::left:  one[0] = input[0]; break;
                case Place::right: one[0] = input[1]; break;
                case Place::mid:   one[0] = eqInputMidSide.getReadPointer (0); break;
                case Place::side:  one[0] = eqInputMidSide.getReadPointer (1); break;
                case Place::stereo:
                default:
                    source = input;
                    numSource = numChannels;
                    break;
            }
        }

        if (slot.dynamic)
            detectSlot (slot, s, source, numSource, numSamples, playingNow, context.tunerOpen);

        if (slot.spectral)
            detectSubBands (slot, s, source, numSource, numSamples, playingNow, context.tunerOpen);

        if (slot.fftSpectral)
        {
            slot.fftThresholdDb = bandThreshold (slot, s, source, numSource, numSamples, playingNow, context.tunerOpen);
            const auto key = playingNow == playingDry.data() ? numSpectralSources - 1 : (numSource > 1 ? 0 : juce::jlimit (0, 4, slot.place));
            sourceA[(size_t) key] = source[0];
            sourceB[(size_t) key] = numSource > 1 ? source[1] : nullptr;
            sourceChannels[(size_t) key] = numSource;
        }
    }

    // Each source heard once; a new spectrum moves the bands that hear it.
    const auto resolution = juce::jlimit (0, 2, linearQuality.getChoiceIndex());
    const auto factor = dsp::LinearPhaseFilter::rateFactor (sampleRate);
    const auto n = dsp::LinearPhaseFilter::baseLength[(size_t) resolution] * factor;
    const auto hop = dsp::LinearPhaseFilter::frameBlocks[(size_t) resolution] * dsp::LinearPhaseFilter::basePartition * factor;

    for (int key = 0; key < numSpectralSources; ++key)
    {
        if (sourceA[(size_t) key] == nullptr)
            continue;

        auto& detector = spectralDetectors[(size_t) key];

        if (detector.getSize() != n || detector.getNumChannels() != sourceChannels[(size_t) key])
        {
            detector.configure (n, hop, sourceChannels[(size_t) key]);
            detector.reset();
        }

        detector.push (sourceA[(size_t) key], sourceB[(size_t) key], numSamples);

        if (! detector.takeNewLevels())
            continue;

        for (int s = 0; s < numSlots; ++s)
        {
            const auto& slot = slots[(size_t) s];

            if (! slot.fftSpectral)
                continue;

            const auto fromDry = slot.p.side->getChoiceIndex() == (int) Side::dryGuitar && context.chainInput != nullptr;
            const auto slotKey = fromDry ? numSpectralSources - 1 : (numChannels > 1 && slot.place != (int) Place::stereo
                                                                     ? juce::jlimit (0, 4, slot.place) : 0);

            if (slotKey == key)
                fftSpectralMoves (s, detector);
        }
    }
}

void StudioEqBlock::spectralRange (int index, double& low, double& high) const noexcept
{
    // Where a spectral band acts: a Bell's bandwidth, a low shelf from 4
    // octaves below its corner to half an octave above, a high shelf the
    // mirror, a Tilt 4 octaves either side.
    const auto& slot = slots[(size_t) index];
    const auto band = getBand (index);
    const auto top = std::min (20000.0, 0.45 * sampleRate);
    const auto f = std::clamp (band.frequency, 20.0, top);

    switch ((Shape) slot.shape)
    {
        case Shape::lowShelf:  low = f / 16.0; high = f * std::sqrt (2.0); break;
        case Shape::highShelf: low = f / std::sqrt (2.0); high = f * 16.0; break;
        case Shape::tiltShelf: low = f / 16.0; high = f * 16.0; break;
        default:
        {
            const auto q = std::max (0.05, band.q);
            const auto bw = 2.0 * std::asinh (1.0 / (2.0 * q)) / std::log (2.0);
            low = f * std::pow (2.0, -0.5 * bw);
            high = f * std::pow (2.0, 0.5 * bw);
            break;
        }
    }

    low = std::clamp (low, 20.0, top);
    high = std::clamp (high, low, top);
}

bool StudioEqBlock::isFftSpectral (int index) const noexcept
{
    return slots[(size_t) index].playing && modeOf (index) == Mode::spectral
           && spectralMethod->getChoiceIndex() == (int) SpectralMethod::fft;
}

float StudioEqBlock::getSpectralGainDb (int slot, double frequency) const noexcept
{
    const auto bins = binMoveSize[(size_t) slot];

    if (bins <= 1)
        return 0.0f;

    const auto n = 2 * (bins - 1);
    const auto bin = juce::jlimit (0, bins - 1, (int) std::lround (frequency * n / sampleRate));
    return binMoves[(size_t) slot * (size_t) maxBins + (size_t) bin] * gainScale.currentValue() * 0.01f;
}

double StudioEqBlock::bandThreshold (Slot& slot, int index, const float* const* source, int numSource, int numSamples,
                                     const std::uint8_t* playing, bool tunerOpen) noexcept
{
    // The band's own sidechain level (Follows or Own), for Auto: learned as a
    // dynamic band's is.
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

    const auto autoThreshold = slot.p.thresh->getValue() < thresholdAutoBelowDb;

    if (! autoThreshold)
        return (double) slot.p.thresh->getValue();

    numSource = juce::jlimit (1, maxChannels, numSource);
    const auto autoSamples = autoWindowSeconds * sampleRate;

    for (int start = 0, c = 0; start < numSamples; start += updateInterval, ++c)
    {
        const auto len = juce::jmin (updateInterval, numSamples - start);
        auto level = 0.0, chunkMs = 0.0;

        for (int ch = 0; ch < numSource; ++ch)
        {
            auto ms = slot.sideMs[(size_t) ch], sum = 0.0;

            for (int i = 0; i < len; ++i)
            {
                const auto y = (double) slot.sideBand.processSample (source[ch][start + i], ch);
                ms = y * y + (ms - y * y) * detectorCoefficient;
                sum += y * y;
            }

            slot.sideMs[(size_t) ch] = ms;
            level = std::max (level, ms);
            chunkMs = std::max (chunkMs, sum / len);
        }

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
    }

    const auto threshold = slot.autoLearned ? 10.0 * std::log10 (slot.autoMs + 1.0e-30) + autoAboveDb
                                            : std::numeric_limits<double>::infinity();
    liveAuto[(size_t) index].store ((float) (slot.autoLearned ? threshold : -std::numeric_limits<double>::infinity()),
                                    std::memory_order_relaxed);
    return threshold;
}

void StudioEqBlock::fftSpectralMoves (int index, const dsp::SpectralDetector& detector) noexcept
{
    auto& slot = slots[(size_t) index];
    const auto n = detector.getSize();
    const auto bins = n / 2 + 1;
    const auto range = (double) slot.p.range->getValue();
    const auto threshold = slot.fftThresholdDb;
    const auto hop = (double) dsp::LinearPhaseFilter::frameBlocks[(size_t) juce::jlimit (0, 2, linearQuality.getChoiceIndex())]
                     * dsp::LinearPhaseFilter::basePartition * dsp::LinearPhaseFilter::rateFactor (sampleRate);
    const auto attack = std::exp (-hop / std::max (1.0, (double) slot.p.attack->getValue() * 0.001 * sampleRate));
    const auto release = std::exp (-hop / std::max (1.0, (double) slot.p.release->getValue() * 0.001 * sampleRate));
    const auto tilt = slot.p.specTilt->getChoiceIndex() == 1;
    const auto f0 = std::max (20.0, getBand (index).frequency);
    double low = 20.0, high = 20000.0;
    spectralRange (index, low, high);
    const auto lowBin = std::max (1, (int) std::ceil (low * n / sampleRate));
    const auto highBin = std::min (bins - 1, (int) std::floor (high * n / sampleRate));
    const auto spreadOctaves = subBandSpacingAt0 * std::pow (2.0, -2.0 * (double) slot.p.density->getValue() / 100.0);
    const auto spread = std::pow (2.0, 0.5 * spreadOctaves) - 1.0;   // a bin's neighbours within half the width, as a share of its frequency
    const auto* l = detector.getLevels (0);
    const auto* r = detector.getLevels (detector.getNumChannels() > 1 ? 1 : 0);
    auto* g = binMovesOf (index);

    if (binMoveSize[(size_t) index] != bins)
    {
        std::fill (g, g + maxBins, 0.0f);
        binMoveSize[(size_t) index] = bins;
    }

    // Targets per bin (magnitude of the movement), spread to the neighbours
    // (Density): a forward and a backward pass, each bin's reach its own.
    auto* target = binTarget.data();
    std::fill (target, target + bins, 0.0f);
    const auto quiet = std::pow (10.0, (threshold - 0.5 * kneeDb) / 10.0);

    for (int k = lowBin; k <= highBin; ++k)
    {
        const auto level = (double) std::max (l[k], r[k]);

        if (range == 0.0 || ! std::isfinite (threshold) || level * (tilt ? 1.41 * (k * sampleRate / n) / f0 : 1.0) < quiet)
            continue;   // under the knee: no movement (the cheap test first)

        const auto f = k * sampleRate / n;
        const auto levelDb = 10.0 * std::log10 (level + 1.0e-30) + (tilt ? spectralTiltDbPerOctave * std::log2 (f / f0) : 0.0);
        target[k] = (float) std::abs (movementDb (levelDb - threshold, range));
    }

    for (int k = lowBin + 1; k <= highBin; ++k)
        target[k] = std::max (target[k], target[k - 1] * (float) std::exp (-1.0 / std::max (0.5, k * spread)));

    for (int k = highBin - 1; k >= lowBin; --k)
        target[k] = std::max (target[k], target[k + 1] * (float) std::exp (-1.0 / std::max (0.5, k * spread)));

    // Attack and Release per bin, in dB.
    const auto sign = range < 0.0 ? -1.0f : 1.0f;

    for (int k = 0; k < bins; ++k)
    {
        const auto t = sign * target[k];
        const auto coefficient = std::abs (t) > std::abs (g[k]) ? attack : release;
        auto m = t + (g[k] - t) * (float) coefficient;

        if (std::abs (m - t) < (float) settledDb)
            m = t;

        g[k] = m;
    }

    slot.fftChanged = true;
}

void StudioEqBlock::layOutSubBands (Slot& slot, int index) noexcept
{
    auto& sb = subBands[(size_t) index];
    const auto band = getBand (index);
    const auto own = slot.p.sideFilter->getChoiceIndex() == (int) SideFilter::own;
    const std::array<double, 5> key { (double) slot.shape, band.frequency, band.q, (double) slot.p.density->getValue(), own ? 1.0 : 0.0 };

    if (key == sb.layoutKey)
        return;

    sb.layoutKey = key;
    const auto f = std::clamp (band.frequency, 20.0, std::min (20000.0, 0.45 * sampleRate));
    double low = f, high = f;
    spectralRange (index, low, high);
    const auto octaves = std::log2 (high / low);
    auto spacing = subBandSpacingAt0 * std::pow (2.0, -2.0 * (double) slot.p.density->getValue() / 100.0);
    auto count = (int) std::lround (octaves / spacing) + 1;

    if (count > maxSubBands)
    {
        count = maxSubBands;
        spacing = octaves / (count - 1);
    }

    count = std::max (1, count);
    const auto step = count > 1 ? octaves / (count - 1) : 0.0;
    const auto bandwidth = std::max (step, spacing);   // octaves: neighbours meet at their edges
    const auto q = 1.0 / (2.0 * std::sinh (std::log (2.0) * bandwidth / 2.0));

    for (int k = 0; k < maxSubBands; ++k)
    {
        auto& sub = sb.subs[(size_t) k];

        if (k >= count)
        {
            sub.moveDb = sub.designedMoveDb = 0.0;
            sub.band.setDesign ({});
            continue;
        }

        sub.frequency = count > 1 ? low * std::pow (2.0, k * step) : f;
        sub.q = q;
        sub.tiltDb = spectralTiltDbPerOctave * std::log2 (sub.frequency / f);
        sub.detector.setDesign (dsp::FilterBandDesign::make ({ dsp::FilterShape::bandPass, sub.frequency, 0.0, q, 12 }, sampleRate));
        sub.designedMoveDb = std::numeric_limits<double>::quiet_NaN();   // redesign its bell with the new place and width
    }

    sb.count = count;

    // The guards, a step beyond each edge, and the unmixing for these places.
    const auto guardStep = count > 1 ? step : spacing;
    std::array<double, maxSubBands + 2> place {};
    place[0] = (count > 1 ? low : f) * std::pow (2.0, -guardStep);

    for (int k = 0; k < count; ++k)
        place[(size_t) k + 1] = sb.subs[(size_t) k].frequency;

    place[(size_t) count + 1] = sb.subs[(size_t) count - 1].frequency * std::pow (2.0, guardStep);
    std::array<dsp::FilterBandDesign, maxSubBands + 2> detectors {};

    for (int j = 0; j < count + 2; ++j)
        detectors[(size_t) j] = dsp::FilterBandDesign::make ({ dsp::FilterShape::bandPass, std::min (place[(size_t) j], 0.49 * sampleRate),
                                                               0.0, q, 12 }, sampleRate);

    sb.guards[0].setDesign (detectors[0]);
    sb.guards[1].setDesign (detectors[(size_t) count + 1]);

    // leakage[j][i]: the power detector j hears from a tone at place i; then invert (Gauss-Jordan).
    const auto m = count + 2;
    std::array<std::array<double, 2 * (maxSubBands + 2)>, maxSubBands + 2> a {};

    for (int j = 0; j < m; ++j)
    {
        for (int i = 0; i < m; ++i)
            a[(size_t) j][(size_t) i] = std::pow (10.0, detectors[(size_t) j].magnitudeDb (std::min (place[(size_t) i], 0.49 * sampleRate), sampleRate) / 10.0);

        for (int i = 0; i < m; ++i)
            a[(size_t) j][(size_t) (m + i)] = i == j ? 1.0 : 0.0;
    }

    for (int col = 0; col < m; ++col)
    {
        auto pivot = col;

        for (int r = col + 1; r < m; ++r)
            if (std::abs (a[(size_t) r][(size_t) col]) > std::abs (a[(size_t) pivot][(size_t) col]))
                pivot = r;

        std::swap (a[(size_t) col], a[(size_t) pivot]);
        const auto d = a[(size_t) col][(size_t) col];

        if (std::abs (d) < 1.0e-12)
            continue;

        for (int i = 0; i < 2 * m; ++i)
            a[(size_t) col][(size_t) i] /= d;

        for (int r = 0; r < m; ++r)
        {
            if (r == col)
                continue;

            const auto factor = a[(size_t) r][(size_t) col];

            for (int i = 0; i < 2 * m; ++i)
                a[(size_t) r][(size_t) i] -= factor * a[(size_t) col][(size_t) i];
        }
    }

    for (int j = 0; j < m; ++j)
        for (int i = 0; i < m; ++i)
            sb.unmix[(size_t) j][(size_t) i] = a[(size_t) j][(size_t) (m + i)];
}

void StudioEqBlock::detectSubBands (Slot& slot, int index, const float* const* source, int numSource, int numSamples,
                                    const std::uint8_t* playing, bool tunerOpen) noexcept
{
    auto& sb = subBands[(size_t) index];
    const auto wanted = modeOf (index) == Mode::spectral && spectralMethod->getChoiceIndex() == (int) SpectralMethod::subBands;
    const auto range = wanted ? (double) slot.p.range->getValue() : 0.0;
    slot.p.range->snapToTarget();

    if (range != 0.0)
        layOutSubBands (slot, index);

    // The band's own sidechain filter: for Auto's level and, with Own, what the sub-bands hear.
    const auto own = slot.p.sideFilter->getChoiceIndex() == (int) SideFilter::own;

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
    const auto tilt = slot.p.specTilt->getChoiceIndex() == 1;
    const auto attackSamples = (double) slot.p.attack->getValue() * 0.001 * sampleRate;
    const auto releaseSamples = (double) slot.p.release->getValue() * 0.001 * sampleRate;
    const auto autoSamples = autoWindowSeconds * sampleRate;
    numSource = juce::jlimit (1, maxChannels, numSource);
    const auto count = sb.count;

    for (int start = 0, c = 0; start < numSamples; start += updateInterval, ++c)
    {
        const auto len = juce::jmin (updateInterval, numSamples - start);
        auto thresholdDb = std::numeric_limits<double>::infinity();

        if (range != 0.0)
        {
            auto bandLevel = 0.0, chunkMs = 0.0;

            for (int ch = 0; ch < numSource; ++ch)
            {
                auto ms = slot.sideMs[(size_t) ch], sum = 0.0;
                const auto* x = source[ch] + start;

                for (int i = 0; i < len; ++i)
                {
                    const auto y = (double) slot.sideBand.processSample (x[i], ch);
                    const auto heard = own ? (float) y : x[i];
                    const auto y2 = y * y;
                    ms = y2 + (ms - y2) * detectorCoefficient;
                    sum += y2;

                    for (int k = 0; k < count; ++k)
                    {
                        auto& sub = sb.subs[(size_t) k];
                        const auto z = (double) sub.detector.processSample (heard, ch);
                        const auto z2 = z * z;
                        sub.ms[(size_t) ch] = z2 + (sub.ms[(size_t) ch] - z2) * detectorCoefficient;
                    }

                    for (int g = 0; g < 2; ++g)
                    {
                        const auto z = (double) sb.guards[(size_t) g].processSample (heard, ch);
                        const auto z2 = z * z;
                        sb.guardMs[(size_t) g][(size_t) ch] = z2 + (sb.guardMs[(size_t) g][(size_t) ch] - z2) * detectorCoefficient;
                    }
                }

                slot.sideMs[(size_t) ch] = ms;
                bandLevel = std::max (bandLevel, ms);
                chunkMs = std::max (chunkMs, sum / len);
            }

            thresholdDb = threshold;

            if (autoThreshold)
            {
                // Auto: the band's own level, learned as a dynamic band's is.
                if (playing[c] != 0 && ! tunerOpen)
                {
                    if (! slot.autoLearned)
                    {
                        slot.autoMs = std::max (bandLevel, chunkMs);
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
        }

        // Each sub-band's own share of what the detectors heard, per channel; the louder channel counts.
        std::array<double, maxSubBands> share {};

        if (range != 0.0 && std::isfinite (thresholdDb))
        {
            for (int ch = 0; ch < numSource; ++ch)
            {
                std::array<double, maxSubBands + 2> heard {};
                heard[0] = sb.guardMs[0][(size_t) ch];

                for (int k = 0; k < count; ++k)
                    heard[(size_t) k + 1] = sb.subs[(size_t) k].ms[(size_t) ch];

                heard[(size_t) count + 1] = sb.guardMs[1][(size_t) ch];

                for (int k = 0; k < count; ++k)
                {
                    auto e = 0.0;

                    for (int i = 0; i < count + 2; ++i)
                        e += sb.unmix[(size_t) k + 1][(size_t) i] * heard[(size_t) i];

                    share[(size_t) k] = std::max (share[(size_t) k], std::max (e, subBandFloorShare * heard[(size_t) k + 1]));
                }
            }
        }

        for (int k = 0; k < count; ++k)
        {
            auto& sub = sb.subs[(size_t) k];
            auto target = 0.0;

            if (range != 0.0 && std::isfinite (thresholdDb))
            {
                const auto level = share[(size_t) k];
                const auto levelDb = 10.0 * std::log10 (level + 1.0e-30) + (tilt ? sub.tiltDb : 0.0);
                target = movementDb (levelDb - thresholdDb, range);
            }

            auto m = sub.moveDb;
            const auto samples = std::abs (target) > std::abs (m) ? attackSamples : releaseSamples;
            m = target + (m - target) * std::exp (-len / std::max (1.0, samples));

            if (std::abs (m - target) < settledDb)
                m = target;

            auto* out = subMovesOf (index, k);
            const auto before = c == 0 ? sub.designedMoveDb : (double) out[c - 1];
            out[c] = (float) m;
            sub.moveDb = (double) out[c];
            slot.subMoveChanged = slot.subMoveChanged || ! (sub.moveDb == before);
        }
    }

    if (autoThreshold && range != 0.0)
        liveAuto[(size_t) index].store (slot.autoLearned ? (float) (10.0 * std::log10 (slot.autoMs + 1.0e-30) + autoAboveDb)
                                                         : -std::numeric_limits<float>::infinity(),
                                        std::memory_order_relaxed);
}

void StudioEqBlock::detectSlot (Slot& slot, int index, const float* const* source, int numSource, int numSamples,
                                const std::uint8_t* playing, bool tunerOpen) noexcept
{
    const auto wanted = modeOf (index) == Mode::dynamic;
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
    // slots it doesn't use are freed. "Tame fizz" and "Pick attack" (B4) are
    // dynamic bells with Thresh Auto.
    constexpr float lowCut = (float) StudioEqBlock::Shape::lowCut, highCut = (float) StudioEqBlock::Shape::highCut;
    constexpr float lowShelf = (float) StudioEqBlock::Shape::lowShelf, bell = (float) StudioEqBlock::Shape::bell;
    constexpr float slope12 = 1.0f, slope18 = 2.0f, slope24 = 3.0f;
    constexpr float dynamic = (float) StudioEqBlock::Mode::dynamic, autoThreshold = -61.0f;

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
    constexpr PresetValue tameFizz[] {
        { "b01Used", 1.0f }, { "b01Shape", bell }, { "b01Freq", 5500.0f }, { "b01Q", 2.0f }, { "b01Gain", 0.0f },
        { "b01Mode", dynamic }, { "b01Range", -4.0f }, { "b01Thresh", autoThreshold }, { "b01Attack", 5.0f }, { "b01Release", 80.0f },
        { "b02Used", 1.0f }, { "b02Shape", highCut }, { "b02Freq", 8000.0f }, { "b02Slope", slope12 },
    };
    constexpr PresetValue pickAttack[] {
        { "b01Used", 1.0f }, { "b01Shape", bell }, { "b01Freq", 2500.0f }, { "b01Q", 1.0f }, { "b01Gain", 0.0f },
        { "b01Mode", dynamic }, { "b01Range", -3.0f }, { "b01Thresh", autoThreshold }, { "b01Attack", 1.0f }, { "b01Release", 60.0f },
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
        { "tameFizz", "Tame fizz", tameFizz },
        { "pickAttack", "Pick attack", pickAttack },
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
    for (const Parameter* setup : std::initializer_list<const Parameter*> { &pan, &width, &invert, &displayRange, &quality, &cpuTight,
                                                                             &phase, &linearQuality, spectralMethod })
        if (&parameter == setup)
            return false;

    return true;
}

} // namespace encore
