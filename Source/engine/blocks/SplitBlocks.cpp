// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "engine/blocks/SplitBlocks.h"

#include <cmath>

namespace encore
{

namespace
{
    juce::String levelText (float dB)
    {
        if (dB <= MergeBlock::minLevelDb)
            return "Off";

        return (dB > 0.0f ? "+" : "") + juce::String (dB, 1) + " dB";
    }

    juce::String panText (float pan)
    {
        const auto p = (int) std::lround (pan);

        if (p == 0)
            return "C";

        return p < 0 ? "L " + juce::String (-p) : "R " + juce::String (p);
    }

    Parameter::Spec level (const char* id, const char* name)
    {
        Parameter::Spec spec { id, name, { MergeBlock::minLevelDb, MergeBlock::maxLevelDb }, 0.0f, "dB", 1 };
        spec.textFunction = levelText;
        return spec;
    }

    Parameter::Spec pan (const char* id, const char* name)
    {
        Parameter::Spec spec { id, name, { -100.0f, 100.0f }, 0.0f, {}, 0 };
        spec.textFunction = panText;
        return spec;
    }

    /** Balance: centre = full on both sides, -100 = left only, +100 = right only. */
    void panGains (float pan, float& left, float& right) noexcept
    {
        const auto p = juce::jlimit (-1.0f, 1.0f, pan * 0.01f);
        left = juce::jmin (1.0f, 1.0f - p);
        right = juce::jmin (1.0f, 1.0f + p);
    }
}

MergeBlock::MergeBlock()
    : Block (typeId, "Merge"),
      levelA (addParameter (level (mergeParams::levelA, "Level A"))),
      panA   (addParameter (pan   (mergeParams::panA,   "Pan A"))),
      levelB (addParameter (level (mergeParams::levelB, "Level B"))),
      panB   (addParameter (pan   (mergeParams::panB,   "Pan B")))
{
}

float MergeBlock::levelToGain (float dB) noexcept
{
    if (! std::isfinite (dB) || dB <= minLevelDb)
        return 0.0f;

    return std::pow (10.0f, juce::jmin (dB, maxLevelDb) * 0.05f);
}

void MergeBlock::mix (const float* const* a, int numA, const float* const* b, int numB,
                      float* const* out, int numSamples, const float* bGain) noexcept
{
    for (auto* p : { &levelA, &panA, &levelB, &panB })
        p->beginBlock();

    const auto* aLeft = a[0];
    const auto* aRight = a[numA > 1 ? 1 : 0];
    const auto* bLeft = b[0];
    const auto* bRight = b[numB > 1 ? 1 : 0];

    const auto moving = levelA.isSmoothing() || panA.isSmoothing() || levelB.isSmoothing() || panB.isSmoothing();
    float aL = 1.0f, aR = 1.0f, bL = 1.0f, bR = 1.0f, gA = 1.0f, gB = 1.0f;

    auto update = [&]
    {
        panGains (panA.nextValue(), aL, aR);
        panGains (panB.nextValue(), bL, bR);
        gA = levelToGain (levelA.nextValue());
        gB = levelToGain (levelB.nextValue());
    };

    if (! moving)
        update();   // steady: work the gains out once

    for (int i = 0; i < numSamples; ++i)
    {
        if (moving)
            update();

        const auto fadeB = bGain != nullptr ? bGain[i] : 1.0f;

        // Read both before writing: out may be path A's own buffers.
        const auto al = aLeft[i], ar = aRight[i], bl = bLeft[i], br = bRight[i];
        out[0][i] = al * gA * aL + bl * gB * fadeB * bL;
        out[1][i] = ar * gA * aR + br * gB * fadeB * bR;
    }
}

} // namespace encore
