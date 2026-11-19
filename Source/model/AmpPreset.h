// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace encore
{

/** An amp built in the amp builder (design/AMP-BUILDER.md): its face (the
    look and the parts on it), one NAM capture and the starting settings of
    the knobs and switches that drive the amp block. Stored in the show (its
    amps list) and as a copy in this computer's amp library. An amp block
    refers to it by id; each patch keeps its own knob values (the face's
    values are only where they start).

    Fields this version doesn't know are kept (`extra`) and written back, so
    a file from a newer Encore stays as it was when this one saves it. */
struct AmpLook
{
    juce::String body { "tall" };        // tall | thin | head | headg | top
    juce::String tolex { "black" };      // black | cream | brown | red | blonde | orange | white | tweed
    juce::String piping { "gold" };      // gold | white | black | none
    juce::String panel { "gold" };       // gold | silver | black | plexi | dark | white | orange
    juce::String grille { "oxblood" };   // oxblood | silver | wheat | salt | black | brass
    juce::String corners { "chrome" };   // chrome | black | none
    bool studs = false;
    juce::var extra;

    bool operator== (const AmpLook&) const;
};

struct AmpPart
{
    juce::String id, type;               // knob | toggle | jewel | section | outline | title | text | symbol | plate
                                         // | badge | handle | jack | screw | logo | trim
    int x = 0, y = 0, w = 0, h = 0;      // face pixels (2 px grid); a knob's size comes from `size`

    // knob: style chicken | black | plain | cream | silver; size S | M | L | XL;
    //       drives input | bass | mid | treble | output | fixed | "" (not set); labelPos above | below;
    //       scale ticks | n10 | n12 | none.
    // toggle: switchStyle bat | push; drives levelMatch | fixed; value 1 = up / out; top, label.
    // jewel (a light): style jewel | led; colour red | amber | green | blue; lights whileOn | off.
    juce::String style, size, drives, label, labelPos, scale, switchStyle, top, colour, lights;
    float value = 0.5f;

    // section / plate / trim: mat; title / text / plate: text; text: font caps | serif;
    // symbol: icon; plate: shape rect | oval; logo: colour, logoSize.
    juce::String mat, text, font, icon, shape;
    int logoSize = 0;

    juce::var extra;

    bool operator== (const AmpPart&) const;

    bool isKnob() const noexcept     { return type == "knob"; }
    bool isToggle() const noexcept   { return type == "toggle"; }
    bool isLight() const noexcept    { return type == "jewel"; }

    /** A knob or switch that drives a parameter of the amp block (not fixed, not unset). */
    bool drivesParameter() const noexcept;
    bool isFixed() const noexcept    { return (isKnob() || isToggle()) && drives == "fixed"; }
    bool isNotSet() const noexcept   { return isKnob() && drives.isEmpty(); }

    /** A light that follows the block's on state ("always off" stays dark). */
    bool lightsWhileOn() const noexcept { return isLight() && lights != "off"; }

    /** Its size on the face: a knob's from S 26 / M 34 / L 48 / XL 64. */
    int width() const noexcept;
    int height() const noexcept;

    /** "Fixed at 6 in this capture" / "Fixed up in this capture" (knobs and switches). */
    juce::String fixedTooltip() const;
};

struct AmpPreset
{
    static constexpr int currentVersion = 1;

    juce::String id, name;
    int version = currentVersion;
    AmpLook look;
    std::vector<AmpPart> parts;
    juce::String capture;                // the NAM file, as the amp block stores it ("models/x.nam" or absolute)
    juce::var extra;

    bool operator== (const AmpPreset&) const;

    const AmpPart* findPart (const juce::String& partId) const;

    /** Whether it can be saved: no knob NOT SET. `why` says what to fix. */
    bool canSave (juce::String* why = nullptr) const;
};

namespace ampPreset
{
    /** A knob size letter's face pixels: S 26, M 34, L 48, XL 64 (M for anything else). */
    int knobSize (const juce::String& letter) noexcept;

    /** What a driving control sets on the NAM amp block (NamAmpBlock's
        parameter ids): input, bass, mid, treble, output; levelMatch → normalise.
        Empty for fixed or unset controls. */
    juce::String parameterFor (const juce::String& drives);

    /** "6", "6.5": a 0..1 value as the face's 0–10 (one decimal, none when whole). */
    juce::String tenth (float value);

    juce::var toVar (const AmpPreset&);
    std::optional<AmpPreset> fromVar (const juce::var&, juce::String& error);

    /** A file in the amp library ("format": "encore-amp"). */
    juce::String toJson (const AmpPreset&);
    std::optional<AmpPreset> fromJson (const juce::String&, juce::String& error);
}

} // namespace encore
