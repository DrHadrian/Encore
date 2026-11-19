// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include "model/AmpPreset.h"

namespace encore
{

/** This computer's amp library (design/AMP-BUILDER.md): a copy of every amp
    saved in the amp builder, one file per amp ("<id>.encore-amp") in the
    app-data folder, so an amp built for one show can be used in another.
    The show keeps its own copy (ShowModel::amps); this one is for picking.
    Files are written whole (via a temporary file) and keep the fields a
    newer Encore added. Message thread. */
class AmpLibrary
{
public:
    explicit AmpLibrary (juce::File folder);

    const juce::File& getFolder() const noexcept { return folder; }
    juce::File fileFor (const juce::String& ampId) const;

    /** Writes (or replaces) the amp's copy. */
    juce::Result save (const AmpPreset&);

    /** Every amp in the library, by name; damaged files are skipped. */
    std::vector<AmpPreset> loadAll() const;
    std::optional<AmpPreset> load (const juce::String& ampId) const;

    bool remove (const juce::String& ampId);

private:
    juce::File folder;
};

} // namespace encore
