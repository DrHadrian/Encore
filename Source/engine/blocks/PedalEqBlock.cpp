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

void PedalEqBlock::prepareBlock (const ProcessSpec&) {}

void PedalEqBlock::resetBlock() noexcept {}

void PedalEqBlock::processBlock (float* const*, int, int, const ProcessContext&) noexcept
{
    // Step 1: parameters only; the signal passes unchanged.
}

} // namespace encore
