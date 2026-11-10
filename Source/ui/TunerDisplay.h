// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/Tuner.h"

namespace encore
{

/** Note name, a needle from -50 to +50 cents, and green when in tune.
    Readable from across a stage at any size. */
class TunerDisplay final : public juce::Component
{
public:
    void setReading (const Tuner::Reading& reading, bool outputMuted, float referencePitch);
    void paint (juce::Graphics&) override;

    /** Click (not when locked): close the tuner. */
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Tuner::Reading shown;
    bool muted = true;
    float reference = 440.0f;
    int framesWithoutNote = 0;
};

} // namespace encore
