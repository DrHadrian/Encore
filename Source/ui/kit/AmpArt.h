// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace encore::kit::ampArt
{

/** The amp builder's art (design/textures/AMP-BUILDER-LAYERS.md), built into
    the app: 227 WebP layers at 2x (body_<layout>_<tolex>_<piping>,
    panel_<layout>_<material>, grille_<layout>_<cloth>,
    corner_<chrome|black>_<tl|tr|bl|br>) and the sprites from TEXTURES.md
    (chicken_strip, knob_black, knob_cream, knob_silver, toggle_lg_on/off,
    jewel_on/off). JUCE decodes WebP itself (JUCE_USE_WEBP).

    Loaded lazily on the message thread and cached by name: the first ask
    decodes, later ones return the same image. Never call it from paint() for
    a file not loaded yet in a hot path, and never from the audio thread
    (AmpFace composes faces once, when an amp is loaded, saved or edited). */

/** A layer or sprite by file name ("body_tall_black_gold.webp",
    "knob_black.png"); a null image when there is no such file. */
juce::Image get (const juce::String& fileName);

/** The pilot jewel, lit, in red (as drawn), amber, green or blue (the
    board's hue-rotate filters, applied once at load); unlit: jewel_off. */
juce::Image jewel (const juce::String& colour, bool lit);

/** Every file built in, sorted. */
juce::StringArray names();

/** Drops the decoded images (they load again when asked). */
void clearCache();
int numCached();

} // namespace encore::kit::ampArt
