// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/AmpPreset.h"

namespace encore::kit
{

/** An amp's face (design/AMP-BUILDER.md §3, board BAmpBuilder): the 600 × 430
    amp box built from the amp builder's layers (body, panel, grille, panel
    sections, feet, corners, studs) and its parts in list order (knobs with
    their ticks, numbers and labels, switches, lights, print, plates, the
    Encore logo, trim).

    compose() renders a face once to an image at 2× and caches it by the
    amp's content: when an amp is loaded, saved or edited, on the message
    thread; never in paint(), never on the audio thread. With
    Layer::staticOnly it leaves out the bodies of the driving knobs and
    switches and the lights that follow the block's on state (their print
    stays): AmpFaceView draws those on top as real controls.

    As a component it shows one composed face, scaled into its bounds. */
class AmpFace final : public juce::Component
{
public:
    /** A body layout's boxes (AMP-BUILDER-LAYERS.md), in face pixels. */
    struct Layout
    {
        int px, py, pw, panelH;              // the panel recess
        bool grille;
        int gx, gw, grilleY, grilleH;        // the grille recess
        juce::Rectangle<int> body;           // the box (corners go at its corners)
        bool feet;                           // rubber feet under a head
    };

    static const Layout& layoutFor (const juce::String& body);

    /** The print colour where a part sits: the panel's, or a panel section's under the part's centre. */
    static juce::Colour printColour (const AmpPreset&, const AmpPart&);
    static juce::Colour ruleColour (const AmpLook&);

    enum class Layer { everything, staticOnly };

    /** A part drawn on top of the static image in Edit: a driving knob or switch, or a light that follows the block. */
    static bool isLive (const AmpPart& p) { return p.drivesParameter() || p.lightsWhileOn(); }

    /** The face as an image (`scale` × 600 × 430), cached by the amp's content. Message thread. */
    static juce::Image compose (const AmpPreset&, Layer, float scale = 2.0f);
    static void clearCache();
    static int numCached();

    /** The same into any Graphics, in face coordinates (the builder draws live while editing). */
    static void paintFace (juce::Graphics&, const AmpPreset&, Layer);
    static void paintBackground (juce::Graphics&, const AmpPreset&);   // body .. studs, sections included
    static void paintPrint (juce::Graphics&, const AmpPreset&, const AmpPart&);   // a part's print (all of a print-only part)

    /** A knob's body, a switch or a light, as it is now: a knob at `value`
        (0..1), a switch up/out when value >= 0.5, a light lit or not. */
    static void paintBody (juce::Graphics&, const AmpPreset&, const AmpPart&, float value, bool lit);

    /** A knob style's picture alone (no shadow, no pointer; a chickenhead at its middle frame), in r: the builder's Knob choice. */
    static void paintKnobPicture (juce::Graphics&, const juce::String& style, juce::Rectangle<float> r);

    /** Where a part's label, top text, ticks and numbers reach (face pixels), for hit areas and selection. */
    static juce::Rectangle<int> partBounds (const AmpPart&);

    AmpFace();

    void setFace (const AmpPreset&, Layer = Layer::everything);
    const juce::Image& getImage() const noexcept { return image; }

    void paint (juce::Graphics&) override;

private:
    juce::Image image;
};

} // namespace encore::kit
