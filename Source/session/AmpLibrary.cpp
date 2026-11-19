// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "session/AmpLibrary.h"

namespace encore
{

namespace
{
    constexpr auto extension = ".encore-amp";

    /** An id as a file name: letters, digits, '-' and '_' only. */
    juce::String safeName (const juce::String& id)
    {
        return id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    }
}

AmpLibrary::AmpLibrary (juce::File f) : folder (std::move (f)) {}

juce::File AmpLibrary::fileFor (const juce::String& ampId) const
{
    return folder.getChildFile (safeName (ampId) + extension);
}

juce::Result AmpLibrary::save (const AmpPreset& amp)
{
    if (safeName (amp.id).isEmpty())
        return juce::Result::fail ("The amp has no id");

    if (! folder.isDirectory() && ! folder.createDirectory())
        return juce::Result::fail ("Couldn't make the amp library folder " + folder.getFullPathName());

    const auto file = fileFor (amp.id);
    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (ampPreset::toJson (amp)) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Couldn't write " + file.getFullPathName());

    return juce::Result::ok();
}

std::vector<AmpPreset> AmpLibrary::loadAll() const
{
    std::vector<AmpPreset> amps;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
    {
        juce::String error;

        if (auto amp = ampPreset::fromJson (file.loadFileAsString(), error))
            amps.push_back (std::move (*amp));
    }

    std::sort (amps.begin(), amps.end(), [] (const AmpPreset& a, const AmpPreset& b)
    {
        const auto byName = a.name.compareNatural (b.name);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });

    return amps;
}

std::optional<AmpPreset> AmpLibrary::load (const juce::String& ampId) const
{
    const auto file = fileFor (ampId);

    if (! file.existsAsFile())
        return std::nullopt;

    juce::String error;
    return ampPreset::fromJson (file.loadFileAsString(), error);
}

bool AmpLibrary::remove (const juce::String& ampId)
{
    return fileFor (ampId).deleteFile();
}

} // namespace encore
