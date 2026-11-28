// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

// The Studio EQ's patch helpers (B3, declared in PatchBuilder.h): kept apart
// from the builder, which needs the plugin host, so they stand on their own.

#include "session/PatchBuilder.h"

namespace encore
{

namespace
{
    constexpr const char* slotSuffixes[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place", "Mode", "Range",
                                             "Thresh", "Attack", "Release", "Side", "SideFilter", "SideFreq", "SideQ" };

    /** A Studio EQ's default values (one made once, on the message thread). */
    float defaultOf (const juce::String& id)
    {
        static const StudioEqBlock defaults;

        if (auto* p = defaults.findParameter (id))
            return p->getSpec().defaultValue;

        return 0.0f;
    }

    bool isUsed (const BlockModel& eq, int slot)
    {
        const auto it = eq.params.find (StudioEqBlock::slotId (slot, "Used"));
        return it != eq.params.end() && it->second >= 0.5f;
    }
}

int addEqBand (BlockModel& eq, const StudioEqBlock::BandSettings& band, juce::String& error)
{
    for (int slot = 0; slot < StudioEqBlock::numSlots; ++slot)
    {
        if (isUsed (eq, slot))
            continue;

        // The slot starts from its defaults, then takes the band.
        for (auto* suffix : slotSuffixes)
            eq.params[StudioEqBlock::slotId (slot, suffix)] = defaultOf (StudioEqBlock::slotId (slot, suffix));

        const auto id = [slot] (const char* suffix) { return StudioEqBlock::slotId (slot, suffix); };
        eq.params[id ("Used")] = 1.0f;
        eq.params[id ("On")] = band.on ? 1.0f : 0.0f;
        eq.params[id ("Shape")] = (float) band.shape;
        eq.params[id ("Freq")] = (float) band.frequency;
        eq.params[id ("Gain")] = (float) band.gainDb;
        eq.params[id ("Q")] = (float) band.q;
        eq.params[id ("Slope")] = (float) band.slope;
        return slot;
    }

    error = "All 24 bands are in use";
    return -1;
}

int removeEqBand (BlockModel& eq, int slot)
{
    if (! juce::isPositiveAndBelow (slot, StudioEqBlock::numSlots))
        return 0;

    int dropped = 0;

    for (auto* suffix : slotSuffixes)
    {
        const auto id = StudioEqBlock::slotId (slot, suffix);
        eq.params[id] = defaultOf (id);
        dropped += (int) eq.expression.erase (id);
    }

    return dropped;
}

void applyEqSlot (const BlockModel& eq, Block& block, int slot)
{
    const auto valueOf = [&eq] (const juce::String& id, float fallback)
    {
        const auto it = eq.params.find (id);
        return it != eq.params.end() ? it->second : fallback;
    };

    auto* used = block.findParameter (StudioEqBlock::slotId (slot, "Used"));
    const auto inUse = valueOf (StudioEqBlock::slotId (slot, "Used"), 0.0f) >= 0.5f;

    // Going out of use: "in use" first, so it glides out at its own settings.
    if (! inUse && used != nullptr)
        used->setValue (0.0f);

    for (auto* suffix : slotSuffixes)
    {
        if (juce::String (suffix) == "Used")
            continue;

        const auto id = StudioEqBlock::slotId (slot, suffix);

        if (auto* p = block.findParameter (id))
            p->setValue (valueOf (id, p->getSpec().defaultValue));
    }

    // Coming into use: the values first, so it glides in at them.
    if (inUse && used != nullptr)
        used->setValue (1.0f);
}

BlockModel convertOldEq (const BlockModel& eq)
{
    if (eq.type != "eq")
        return eq;

    auto converted = eq;   // id, on/off, stomp, everything else stays
    converted.type = StudioEqBlock::typeId;
    converted.params.clear();
    converted.expression.clear();
    converted.compare = {};

    const auto valueOf = [&eq] (const char* id, float fallback)
    {
        const auto it = eq.params.find (id);
        return it != eq.params.end() ? it->second : fallback;
    };

    const auto onPedal = [&eq] (const char* id) { return eq.expression.count (id) > 0; };
    int slot = 0;

    // A band, and the old ids whose pedals move to its parameters.
    const auto add = [&] (StudioEqBlock::Shape shape, double f, double gain, double q, std::initializer_list<std::pair<const char*, const char*>> moves)
    {
        StudioEqBlock::BandSettings band;
        band.shape = shape;
        band.frequency = f;
        band.gainDb = gain;
        band.q = q;
        band.slope = 1;   // 12 dB/oct
        juce::String error;
        const auto at = addEqBand (converted, band, error);

        for (const auto& [oldId, suffix] : moves)
            if (const auto it = eq.expression.find (oldId); it != eq.expression.end())
                converted.expression[StudioEqBlock::slotId (at, suffix)] = it->second;

        ++slot;
    };

    using S = StudioEqBlock::Shape;
    const auto lowCut = valueOf ("lowCut", 20.0f), highCut = valueOf ("highCut", 20000.0f);
    const auto bass = valueOf ("bass", 0.0f), mid = valueOf ("mid", 0.0f), treble = valueOf ("treble", 0.0f);

    if (lowCut > 20.5f || onPedal ("lowCut"))
        add (S::lowCut, lowCut, 0.0, 0.7071, { { "lowCut", "Freq" } });

    if (std::abs (bass) > 0.05f || onPedal ("bass"))
        add (S::lowShelf, 120.0, bass, 0.7071, { { "bass", "Gain" } });

    if (std::abs (mid) > 0.05f || onPedal ("mid") || onPedal ("midFreq") || onPedal ("midQ"))
        add (S::bell, valueOf ("midFreq", 800.0f), mid, valueOf ("midQ", 0.9f), { { "mid", "Gain" }, { "midFreq", "Freq" }, { "midQ", "Q" } });

    if (std::abs (treble) > 0.05f || onPedal ("treble"))
        add (S::highShelf, 3500.0, treble, 0.7071, { { "treble", "Gain" } });

    if (highCut < 19900.0f || onPedal ("highCut"))
        add (S::highCut, highCut, 0.0, 0.7071, { { "highCut", "Freq" } });

    juce::ignoreUnused (slot);
    return converted;
}

} // namespace encore
