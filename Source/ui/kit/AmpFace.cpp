// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "ui/kit/AmpFace.h"

#include "ui/kit/AmpArt.h"
#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreAmpTheme.h"
#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

#include <cmath>
#include <map>

namespace encore::kit
{

using namespace theme;

namespace
{
    juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    //==========================================================================
    /** CSS radial-gradient(circle at fx fy, ...): centred at a fraction of the box, reaching its farthest corner. */
    juce::ColourGradient radial (juce::Rectangle<float> r, float fx, float fy, std::initializer_list<std::pair<double, juce::uint32>> stops)
    {
        const juce::Point<float> centre (r.getX() + r.getWidth() * fx, r.getY() + r.getHeight() * fy);
        const auto dx = juce::jmax (centre.x - r.getX(), r.getRight() - centre.x);
        const auto dy = juce::jmax (centre.y - r.getY(), r.getBottom() - centre.y);
        const auto radius = std::sqrt (dx * dx + dy * dy);

        juce::ColourGradient g (c (stops.begin()->second), centre, c (stops.begin()->second), centre.translated (radius, 0.0f), true);
        g.clearColours();

        for (const auto& [at, colour] : stops)
            g.addColour (at, c (colour));

        return g;
    }

    juce::ColourGradient linear (float x1, float y1, float x2, float y2, std::initializer_list<std::pair<double, juce::uint32>> stops)
    {
        juce::ColourGradient g (c (stops.begin()->second), x1, y1, c (stops.begin()->second), x2, y2, false);
        g.clearColours();

        for (const auto& [at, colour] : stops)
            g.addColour (at, c (colour));

        return g;
    }

    void dropShadow (juce::Graphics& g, const juce::Path& shape, juce::uint32 colour, float blur, juce::Point<int> offset)
    {
        juce::DropShadow (c (colour), juce::jmax (1, juce::roundToInt (blur)), offset).drawForPath (g, shape);
    }

    juce::Path ellipse (juce::Rectangle<float> r)  { juce::Path p; p.addEllipse (r); return p; }

    /** A round part's soft shadow (CSS drop-shadow on a circle): a radial fall-off
        from the edge outwards over `blur` px, `dy` px lower. Exact and cheap. */
    void circleShadow (juce::Graphics& g, juce::Rectangle<float> r, juce::uint32 colour, float blur, float dy)
    {
        const auto radius = r.getWidth() * 0.5f;
        const auto outer = radius + blur;
        const auto centre = r.getCentre().translated (0.0f, dy);
        juce::ColourGradient fall (c (colour), centre, c (colour).withAlpha (0.0f), centre.translated (outer, 0.0f), true);
        fall.clearColours();
        fall.addColour (0.0, c (colour));
        fall.addColour (juce::jlimit (0.0, 0.99, (double) ((radius - blur) / outer)), c (colour));
        fall.addColour (1.0, c (colour).withAlpha (0.0f));
        g.setGradientFill (fall);
        g.fillEllipse (juce::Rectangle<float> (outer * 2.0f, outer * 2.0f).withCentre (centre));
    }
    juce::Path rounded (juce::Rectangle<float> r, float radius) { juce::Path p; p.addRoundedRectangle (r, radius); return p; }

    //==========================================================================
    // Text on the faces: Inter ExtraBold caps with the board's tracking (all face print; the
    // serif style was dropped on 20 Nov 2026, so an older file's "font": "serif" draws as caps).
    juce::Font caps (float px, float trackingEm) { return fontOfSize (type::Weight::extraBold, px, trackingEm); }

    /** Lines ('\n') centred on `centreX`, the first starting at `top`, line height `lineH`. */
    void centredLines (juce::Graphics& g, const juce::String& text, float centreX, float top, float lineH, const juce::Font& font, juce::Colour colour)
    {
        g.setFont (font);
        g.setColour (colour);
        auto y = top;

        for (const auto& line : juce::StringArray::fromLines (text))
        {
            const auto w = juce::jmax (1.0f, textWidth (font, line)) + 4.0f;
            g.drawText (line, juce::Rectangle<float> (centreX - w * 0.5f, y, w, lineH), juce::Justification::centred, false);
            y += lineH;
        }
    }

    int numLines (const juce::String& text) { return juce::jmax (1, juce::StringArray::fromLines (text).size()); }

    //==========================================================================
    // The ten printed symbols (24 × 24, stroke 2, round caps and joins).
    const juce::Path& symbolPath (const juce::String& icon)
    {
        static const std::map<juce::String, juce::Path> paths = []
        {
            std::map<juce::String, juce::Path> m;
            const auto svg = [] (std::initializer_list<const char*> ds)
            {
                juce::Path p;

                for (auto* d : ds)
                    p.addPath (juce::Drawable::parseSVGPath (d));

                return p;
            };

            m["volume"] = svg ({ "M4 9h4l5-4v14l-5-4H4z", "M16.5 9a4 4 0 0 1 0 6", "M19 6.5a8 8 0 0 1 0 11" });
            m["treble"] = svg ({ "M2 16l2.5-5 2.5 5 2.5-5 2.5 5 2.5-5 2.5 5 2.5-5 2.5 5", "M12 7V2M9.5 4.5 12 2l2.5 2.5" });
            m["bass"]   = svg ({ "M2 11c2.5-6 7.5-6 10 0s7.5 6 10 0", "M12 17v5M9.5 19.5 12 22l2.5-2.5" });
            m["mid"]    = svg ({ "M2 12h6M16 12h6M8 7l4 5-4 5M16 7l-4 5 4 5" });
            m["gain"]   = svg ({ "M3 18 8 6v12l5-12v12l5-12v12" });
            m["reverb"] = svg ({ "M2 12c0-5 4-5 4 0s4 5 4 0 4-5 4 0 4 5 4 0 4-5 4 0" });
            m["power"]  = svg ({ "M9 3v5M15 3v5", "M6 8h12v3a6 6 0 0 1-12 0z", "M12 17v4" });
            auto input = svg ({ "M2 12h8", "M17 12h5" });
            input.addRoundedRectangle (10.0f, 9.0f, 7.0f, 6.0f, 1.0f);
            m["input"]  = input;
            m["clean"]  = svg ({ "M2 12c2.5-7 6.5-7 9 0s6.5 7 9 0" });
            m["dirty"]  = svg ({ "M2 16h4V8h6v8h6V8h4" });
            return m;
        }();

        static const juce::Path none;
        const auto found = paths.find (icon);
        return found != paths.end() ? found->second : none;
    }

    //==========================================================================
    struct Print { juce::uint32 print, rule; };

    Print printFor (const juce::String& panel)
    {
        if (panel == "white")  return { amp::printDark, amp::ruleWhite };
        if (panel == "orange") return { amp::printDark, amp::ruleOrange };
        if (panel == "plexi")  return { amp::printPlexi, amp::rulePlexi };
        if (panel == "silver") return { amp::printSilver, amp::ruleSilver };
        if (panel == "black")  return { amp::printLight, amp::ruleBlack };
        if (panel == "dark")   return { amp::printLight, amp::ruleDark };
        return { amp::printGold, amp::ruleGold };
    }

    juce::uint32 glowFor (const juce::String& colour)
    {
        if (colour == "amber") return amp::glowAmber;
        if (colour == "green") return amp::glowGreen;
        if (colour == "blue")  return amp::glowBlue;
        return amp::glowRed;
    }

    const juce::uint32* ledFor (const juce::String& colour)
    {
        if (colour == "amber") return amp::ledAmber;
        if (colour == "green") return amp::ledGreen;
        if (colour == "blue")  return amp::ledBlue;
        return amp::ledRed;
    }

    juce::uint32 logoColourFor (const juce::String& colour)
    {
        if (colour == "white") return amp::logoWhite;
        if (colour == "gold")  return amp::logoGold;
        if (colour == "black") return amp::logoBlack;
        return amp::logoCream;
    }

    void drawImageInto (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> r)
    {
        if (image.isValid())
            g.drawImage (image, r, juce::RectanglePlacement::stretchToFit);
    }

    //==========================================================================
    // Knobs: the scale and label sizes (board: knob() and its print).
    struct KnobPrint
    {
        int numbers;          // 0, 10 or 12
        bool ticks;
        float numberPx, labelPx, labelTracking;
    };

    KnobPrint knobPrint (const AmpPart& p)
    {
        const auto s = (float) p.width();
        const auto scale = p.scale.isNotEmpty() ? p.scale : juce::String ("none");
        return { scale == "n10" ? 10 : scale == "n12" ? 12 : 0, scale == "ticks", s < 40.0f ? 6.0f : 7.5f,
                 s < 32.0f ? 6.5f : 9.5f, s < 32.0f ? 0.02f : 0.1f };
    }

    void paintKnobPrint (juce::Graphics& g, const AmpPart& p, juce::Colour pc)
    {
        const auto s = (float) p.width();
        const juce::Point<float> centre ((float) p.x + s * 0.5f, (float) p.y + s * 0.5f);
        const auto kp = knobPrint (p);

        if (kp.ticks)
        {
            // 11 ticks over 270°; the ends and the middle longer.
            const auto small = s < 40.0f;
            g.setColour (pc);

            for (int i = 0; i <= 10; ++i)
            {
                const auto big = i == 0 || i == 5 || i == 10;
                const auto len = small ? (big ? 3.0f : 2.0f) : (big ? 5.0f : 3.5f);
                const auto w = small ? (big ? 1.2f : 0.9f) : (big ? 1.6f : 1.0f);
                const auto inner = s * 0.5f + (small ? 1.5f : 2.0f);
                juce::Path tick;
                tick.addRoundedRectangle (-w * 0.5f, -(inner + len), w, len, 0.5f);
                g.fillPath (tick, juce::AffineTransform::rotation (juce::degreesToRadians (-135.0f + 27.0f * (float) i)).translated (centre));
            }
        }

        if (kp.numbers > 0)
        {
            const auto font = fontOfSize (type::Weight::bold, kp.numberPx, 0.0f, true);
            const auto rn = s * 0.5f + 2.0f + kp.numberPx * 0.6f;
            g.setFont (font);
            g.setColour (pc);

            for (int n = 1; n <= kp.numbers; ++n)
            {
                const auto a = juce::degreesToRadians (-135.0f + 270.0f * (float) (n - 1) / (float) (kp.numbers - 1));
                const juce::Point<float> at (centre.x + rn * std::sin (a), centre.y - rn * std::cos (a));
                g.drawText (juce::String (n), juce::Rectangle<float> (20.0f, kp.numberPx + 2.0f).withCentre (at), juce::Justification::centred, false);
            }
        }

        if (p.label.isNotEmpty())
        {
            const auto lineH = kp.labelPx * 1.15f;
            const auto lines = (float) numLines (p.label);
            const auto top = p.labelPos == "above"
                                 ? (float) p.y - (kp.numbers > 0 ? 11.0f : kp.ticks ? 9.0f : 3.0f) - lineH * lines
                                 : (float) p.y + s + (kp.numbers > 0 ? 6.0f : kp.ticks ? 8.0f : 4.0f);
            centredLines (g, p.label, centre.x, top, lineH, caps (kp.labelPx, kp.labelTracking), pc);
        }
    }

    /** A knob's body without its pointer (board: the knob element, `border-radius: 50%`,
        which clips its picture to the circle), drawn at r. */
    void paintKnobCap (juce::Graphics& g, const juce::String& style, juce::Rectangle<float> r, int frame)
    {
        const auto s = r.getWidth();
        const auto circle = ellipse (r);
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (circle);

        if (style == "chicken")
        {
            // The chickenhead strip: 41 frames for 0..1, one per knob width; its baked contact shadow inside the circle.
            if (const auto strip = ampArt::get ("chicken_strip.png"); strip.isValid())
            {
                const auto frameSize = strip.getWidth();
                const auto at = juce::jlimit (0, strip.getHeight() / frameSize - 1, frame);
                g.drawImage (strip.getClippedImage ({ 0, at * frameSize, frameSize, frameSize }), r, juce::RectanglePlacement::stretchToFit);
            }

            return;
        }

        if (style == "plain")
        {
            g.setGradientFill (radial (r, 0.4f, 0.32f, { { 0.0, amp::plainLight }, { 0.38, amp::plainMid }, { 0.70, amp::plainDark }, { 1.0, amp::plainDark } }));
            g.fillPath (circle);
            const auto ring = juce::jmax (1.0f, s / 14.0f);
            g.setColour (c (amp::plainRing));
            g.drawEllipse (r.reduced (ring * 0.5f), ring);
            g.setColour (c (amp::plainShine));
            g.drawEllipse (r.reduced (0.5f).translated (0.0f, 1.0f), 1.0f);
            return;
        }

        const auto file = style == "cream" ? "knob_cream.png" : style == "silver" ? "knob_silver.png" : "knob_black.png";
        drawImageInto (g, ampArt::get (file), r);
    }

    /** The knob's CSS `filter: drop-shadow(0 Y B rgba(0,0,0,0.55))`, Y = max(2, s/18), B = max(1, s/30):
        the cap's alpha blurred (Gaussian, sigma B/2) and moved Y down. Made once per look, size,
        frame and pixel scale, and kept (the cache is bounded). */
    struct KnobShadowCache final : private juce::DeletedAtShutdown
    {
        ~KnobShadowCache() override { clearSingletonInstance(); }
        std::map<juce::String, juce::Image> images;
        JUCE_DECLARE_SINGLETON_INLINE (KnobShadowCache, false)
    };

    void paintKnobShadow (juce::Graphics& g, const juce::String& style, juce::Rectangle<float> r, int frame)
    {
        const auto s = r.getWidth();
        const auto dy = juce::jmax (amp::knobShadowMinY, s / amp::knobShadowYDiv);
        const auto blur = juce::jmax (amp::knobShadowMinBlur, s / amp::knobShadowBlurDiv);
        const auto pad = std::ceil (blur * 2.0f) + 1.0f;   // room for the blur (3 sigma) around the cap
        const auto scale = juce::jlimit (1.0f, 4.0f, std::ceil (g.getInternalContext().getPhysicalPixelScaleFactor()));
        const auto key = style + "|" + juce::String (s) + "|" + juce::String (style == "chicken" ? frame : 0) + "|" + juce::String (scale);
        auto& images = KnobShadowCache::getInstance()->images;
        auto it = images.find (key);

        if (it == images.end())
        {
            if (images.size() > (size_t) amp::knobShadowCacheMax)
                images.clear();

            const auto side = juce::roundToInt ((s + pad * 2.0f) * scale);
            juce::Image alpha (juce::Image::ARGB, side, side, true);
            {
                juce::Graphics ag (alpha);
                ag.addTransform (juce::AffineTransform::scale (scale));
                paintKnobCap (ag, style, { pad, pad, s, s }, frame);
            }

            const auto sigma = blur * 0.5f * scale;
            juce::ImageConvolutionKernel kernel (juce::jmax (3, 2 * (int) std::ceil (sigma * 3.0f) + 1));
            kernel.createGaussianBlur (sigma);
            juce::Image blurred (juce::Image::ARGB, side, side, true);

            {
                // Black, with the cap's alpha × 0.55.
                const juce::Image::BitmapData src (alpha, juce::Image::BitmapData::readWrite);
                for (int y = 0; y < side; ++y)
                    for (int x = 0; x < side; ++x)
                        src.setPixelColour (x, y, c (amp::knobShadow).withMultipliedAlpha (src.getPixelColour (x, y).getFloatAlpha()));
            }

            kernel.applyToImage (blurred, alpha, blurred.getBounds());
            it = images.emplace (key, blurred).first;
        }

        g.drawImage (it->second, juce::Rectangle<float> (r.getX() - pad, r.getY() - pad + dy, s + pad * 2.0f, s + pad * 2.0f));
    }

    void paintKnobBody (juce::Graphics& g, const AmpPart& p, float value)
    {
        const auto s = (float) p.width();
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, s, s);
        const auto frame = juce::roundToInt (value * 40.0f);   // the chickenhead's 41 frames

        paintKnobShadow (g, p.style, r, frame);
        paintKnobCap (g, p.style, r, frame);

        if (p.style == "chicken")
            return;   // its pointer is in the picture

        // The pointer: 2 px #F2F2F4, radius 1, round(s × 0.36) from the centre, -135° .. +135°, a 1 px dark halo.
        const auto length = (float) juce::roundToInt (s * 0.36f);
        juce::Path line;
        line.addRoundedRectangle (-1.0f, -length, 2.0f, length, 1.0f);
        const auto turn = juce::AffineTransform::rotation (juce::degreesToRadians (-135.0f + 270.0f * value)).translated (r.getCentre());
        g.setColour (c (amp::pointerShade));
        g.strokePath (line, juce::PathStrokeType (0.6f), turn);
        g.setColour (c (amp::pointer));
        g.fillPath (line, turn);
    }

    //==========================================================================
    void paintSwitchPrint (juce::Graphics& g, const AmpPart& p, juce::Colour pc)
    {
        const auto small = p.h < 40;
        const auto px = small ? 6.5f : 9.5f, tracking = small ? 0.02f : 0.1f, lineH = px * 1.15f;
        const auto centreX = (float) p.x + (float) p.w * 0.5f;

        if (p.top.isNotEmpty())
            centredLines (g, p.top, centreX, (float) p.y - 3.0f - lineH * (float) numLines (p.top), lineH, caps (px, tracking), pc);

        if (p.label.isNotEmpty())
            centredLines (g, p.label, centreX, (float) (p.y + p.h) + 3.0f, lineH, caps (px, tracking), pc);
    }

    void paintSwitchBody (juce::Graphics& g, const AmpPart& p, bool up)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);

        if (p.switchStyle != "push")
        {
            drawImageInto (g, ampArt::get (up ? "toggle_lg_on.png" : "toggle_lg_off.png"), r);
            return;
        }

        // The push button: a chrome bezel and a black cap; pressed in, it sits lower and darker.
        const auto circle = ellipse (r);
        circleShadow (g, r, amp::switchShadow, up ? 2.0f : 1.0f, up ? 2.0f : 1.0f);

        if (up)
            g.setGradientFill (radial (r, 0.44f, 0.38f, { { 0.0, amp::pushCapLight }, { 0.38, amp::pushCapMid }, { 0.52, amp::pushCapDark },
                                                          { 0.55, amp::pushGap }, { 0.60, amp::pushChromeLight }, { 0.74, amp::pushChromeMid },
                                                          { 0.92, amp::pushChromeDark }, { 1.0, amp::pushEdge } }));
        else
            g.setGradientFill (radial (r, 0.5f, 0.54f, { { 0.0, amp::pushInCap }, { 0.40, amp::pushInCapMid }, { 0.54, amp::pushInCapDark },
                                                         { 0.58, amp::pushInGap }, { 0.66, amp::pushInChrome }, { 0.78, amp::pushInChromeMid },
                                                         { 0.92, amp::pushInChromeDark }, { 1.0, amp::pushInEdge } }));

        g.fillPath (circle);
    }

    //==========================================================================
    void paintLightBody (juce::Graphics& g, const AmpPart& p, bool lit)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);

        if (p.style == "led")
        {
            // A small flat LED in a chrome bezel.
            const auto* led = ledFor (p.colour);
            const auto circle = ellipse (r);

            if (lit)
                dropShadow (g, circle, led[0], juce::jmax (4.0f, r.getWidth()), {});

            g.setColour (c (amp::ledBezel));
            g.fillEllipse (r.expanded (2.0f));
            g.setColour (c (amp::ledBezelInner));
            g.fillEllipse (r.expanded (1.0f));

            if (lit)
                g.setGradientFill (radial (r, 0.4f, 0.35f, { { 0.0, amp::ledHot }, { 0.32, led[0] }, { 0.80, led[1] }, { 1.0, led[1] } }));
            else
                g.setGradientFill (radial (r, 0.4f, 0.35f, { { 0.0, amp::ledUnlitShine }, { 0.45, led[2] }, { 0.90, amp::ledUnlitEdge }, { 1.0, amp::ledUnlitEdge } }));

            g.fillPath (circle);
            return;
        }

        // The pilot jewel, its colour made from the red one; lit, it glows.
        const auto image = ampArt::jewel (p.colour, lit);

        if (! image.isValid())
            return;

        if (lit)
        {
            const juce::Graphics::ScopedSaveState state (g);
            const auto k = r.getWidth() / (float) image.getWidth();
            g.addTransform (juce::AffineTransform::scale (k).translated (r.getX(), r.getY()));
            juce::DropShadow (c (glowFor (p.colour)), juce::jmax (1, juce::roundToInt (r.getWidth() / 3.0f / k)), {}).drawForImage (g, image);
        }

        drawImageInto (g, image, r);
    }

    //==========================================================================
    void paintPlate (juce::Graphics& g, const AmpPart& p)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);
        const auto oval = p.shape == "oval";
        const auto shape = oval ? ellipse (r) : rounded (r, 3.0f);
        juce::uint32 text = amp::plateGoldText;

        if (p.mat == "blackgold")
        {
            dropShadow (g, shape, amp::plateRimShadow, 3.0f, { 0, 2 });
            g.setGradientFill (radial (r, 0.5f, 0.35f, { { 0.0, amp::plateRimCentre }, { 0.70, amp::plateRimEdge }, { 1.0, amp::plateRimEdge } }));
            g.fillPath (shape);

            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape);
            const auto rim = p.h > 40 ? 3.0f : 2.0f;
            g.setColour (c (amp::plateRimInner));
            g.strokePath (oval ? ellipse (r.reduced (rim + 0.5f)) : rounded (r.reduced (rim + 0.5f), 3.0f), juce::PathStrokeType (1.0f));
            g.setColour (c (amp::plateRim));
            g.strokePath (oval ? ellipse (r.reduced (rim * 0.5f)) : rounded (r.reduced (rim * 0.5f), 3.0f), juce::PathStrokeType (rim));
            text = amp::plateRimText;
        }
        else
        {
            juce::uint32 top = amp::plateGoldTop, bottom = amp::plateGoldBottom, shine = amp::plateGoldShine;

            if (p.mat == "silver")     { top = amp::plateSilverTop; bottom = amp::plateSilverBottom; shine = amp::plateSilverShine; text = amp::plateSilverText; }
            else if (p.mat == "black") { top = amp::plateBlackTop;  bottom = amp::plateBlackBottom;  shine = amp::plateBlackShine;  text = amp::plateBlackText; }

            dropShadow (g, shape, amp::plateShadow, p.mat == "gold" || p.mat.isEmpty() ? 3.0f : 2.0f, { 0, 1 });
            g.setGradientFill (linear (r.getX(), r.getY(), r.getX(), r.getBottom(), { { 0.0, top }, { 1.0, bottom } }));
            g.fillPath (shape);

            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape);
            g.setColour (c (shine));
            g.fillRect (r.withHeight (1.0f));
        }

        if (p.text.isNotEmpty())
        {
            g.setFont (caps (juce::jmax (7.0f, std::round ((float) p.h * 0.5f)), 0.08f));
            g.setColour (c (text));
            g.drawText (p.text, r.expanded (20.0f, 0.0f), juce::Justification::centred, false);
        }
    }

    void paintTrim (juce::Graphics& g, const AmpPart& p)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);
        const auto shape = rounded (r, 2.0f);
        const auto down = [&r] (std::initializer_list<std::pair<double, juce::uint32>> stops) { return linear (r.getX(), r.getY(), r.getX(), r.getBottom(), stops); };

        if (p.mat == "gold")
            g.setGradientFill (down ({ { 0.0, amp::trimGold[0] }, { 0.5, amp::trimGold[1] }, { 0.6, amp::trimGold[2] }, { 1.0, amp::trimGold[3] } }));
        else if (p.mat == "black")
            g.setGradientFill (down ({ { 0.0, amp::trimBlackTop }, { 1.0, amp::trimBlackBottom } }));
        else
        {
            dropShadow (g, shape, amp::plateShadow, 2.0f, { 0, 1 });
            g.setGradientFill (down ({ { 0.0, amp::trimSilver[0] }, { 0.45, amp::trimSilver[1] }, { 0.6, amp::trimSilver[2] }, { 1.0, amp::trimSilver[3] } }));
        }

        g.fillPath (shape);
    }

    void paintHandle (juce::Graphics& g, const AmpPart& p)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);
        const auto bar = rounded (r.reduced (1.0f, 0.0f), 6.0f);
        dropShadow (g, bar, amp::handleShadow, 3.0f, { 1, 2 });
        g.setGradientFill (linear (r.getX(), 0.0f, r.getRight(), 0.0f, { { 0.0, amp::handleDark }, { 0.35, amp::handleShine }, { 0.6, amp::handleMid }, { 1.0, amp::handleEdge } }));
        g.fillPath (bar);

        for (const auto cap : { r.withHeight (10.0f), r.withTrimmedTop (r.getHeight() - 10.0f) })
        {
            const auto box = cap.expanded (2.0f, 0.0f);
            g.setGradientFill (linear (0.0f, box.getY(), 0.0f, box.getBottom(), { { 0.0, amp::handleCapTop }, { 1.0, amp::handleCapBottom } }));
            g.fillRoundedRectangle (box, 3.0f);
        }
    }

    void paintPrintOnly (juce::Graphics& g, const AmpPreset& preset, const AmpPart& p, juce::Colour pc)
    {
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);

        if (p.type == "jack")
        {
            const auto circle = ellipse (r);
            circleShadow (g, r, amp::jackShadow, 2.0f, 1.0f);
            g.setGradientFill (radial (r, 0.5f, 0.5f, { { 0.0, amp::jackHole }, { 0.28, amp::jackHole }, { 0.31, amp::jackLight },
                                                       { 0.55, amp::jackMid }, { 0.75, amp::jackDark }, { 1.0, amp::jackEdge } }));
            g.fillPath (circle);
        }
        else if (p.type == "screw")
        {
            const auto circle = ellipse (r);
            circleShadow (g, r, amp::screwShadow, 1.0f, 1.0f);
            g.setGradientFill (radial (r, 0.35f, 0.3f, { { 0.0, amp::screwLight }, { 0.7, amp::screwDark }, { 1.0, amp::screwDark } }));
            g.fillPath (circle);
        }
        else if (p.type == "symbol")
        {
            const auto& path = symbolPath (p.icon);
            g.setColour (pc);
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
        }
        else if (p.type == "badge")
        {
            g.setColour (c (amp::badgeFill));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (pc);
            g.drawRoundedRectangle (r.reduced (0.75f), 2.0f, 1.5f);
            const auto size = std::round (juce::jmin (r.getWidth(), r.getHeight()) * 0.62f);
            drawClef (g, juce::Rectangle<float> (size, size).withCentre (r.getCentre()), pc);
        }
        else if (p.type == "handle")
        {
            paintHandle (g, p);
        }
        else if (p.type == "outline")
        {
            g.setColour (pc);
            g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.5f);
        }
        else if (p.type == "title")
        {
            const auto font = caps (10.0f, 0.16f);
            const auto w = textWidth (font, p.text);
            const auto gap = p.text.isEmpty() ? 0.0f : 8.0f;
            const auto ruleW = juce::jmax (0.0f, (r.getWidth() - w - 2.0f * gap) * 0.5f);
            const auto y = std::round (r.getCentreY());
            g.setColour (AmpFace::ruleColour (preset.look));
            g.fillRect (juce::Rectangle<float> (r.getX(), y, ruleW, 1.0f));
            g.fillRect (juce::Rectangle<float> (r.getRight() - ruleW, y, ruleW, 1.0f));
            g.setFont (font);
            g.setColour (pc);
            g.drawText (p.text, r.withTrimmedLeft (ruleW + gap).withTrimmedRight (ruleW + gap).expanded (2.0f, 0.0f), juce::Justification::centred, false);
        }
        else if (p.type == "text")
        {
            const auto px = juce::jmax (6.0f, std::round ((float) p.h * 0.78f));
            g.setFont (caps (px, p.h < 12 ? 0.05f : 0.12f));
            g.setColour (pc);
            g.drawText (p.text, r.expanded (40.0f, 0.0f), juce::Justification::centred, false);
        }
        else if (p.type == "plate")
        {
            paintPlate (g, p);
        }
        else if (p.type == "trim")
        {
            paintTrim (g, p);
        }
        else if (p.type == "logo")
        {
            drawLogo (g, r, c (logoColourFor (p.colour)), juce::RectanglePlacement (juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop));
        }
    }

    void paintSection (juce::Graphics& g, const AmpPreset& preset, const AmpPart& p)
    {
        // A block of another panel material: the panel image of that material, offset so they share one texture.
        const auto& lay = AmpFace::layoutFor (preset.look.body);
        const auto r = juce::Rectangle<float> ((float) p.x, (float) p.y, (float) p.w, (float) p.h);
        const auto radius = (float) amp::sectionRadius;
        const auto shape = rounded (r, radius);

        dropShadow (g, shape, amp::sectionDrop, 3.0f, { 0, 2 });
        g.setColour (c (amp::sectionRing));
        g.fillRoundedRectangle (r.expanded (1.0f), radius + 1.0f);

        {
            const juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape);
            drawImageInto (g, ampArt::get ("panel_" + preset.look.body + "_" + (p.mat.isNotEmpty() ? p.mat : juce::String ("black")) + ".webp"),
                           juce::Rectangle<float> ((float) lay.px, (float) lay.py, (float) lay.pw, (float) lay.panelH));
            g.setColour (c (amp::sectionShine));
            g.fillRect (r.withHeight (1.0f));
            g.setColour (c (amp::sectionShade));
            g.fillRect (r.withTrimmedTop (r.getHeight() - 1.0f));
        }
    }

    //==========================================================================
    struct CacheEntry { juce::Image image; juce::uint32 lastUsed = 0; };

    /** Composed faces, by content (a few amps per show: the 24 used last); let go at JUCE's shutdown. */
    struct FaceCache final : private juce::DeletedAtShutdown
    {
        ~FaceCache() override { clearSingletonInstance(); }
        std::map<juce::String, CacheEntry> entries;
        JUCE_DECLARE_SINGLETON_INLINE (FaceCache, false)
    };

    std::map<juce::String, CacheEntry>& faceCache()
    {
        return FaceCache::getInstance()->entries;
    }

    constexpr size_t maxCachedFaces = 24;
}

//==============================================================================
const AmpFace::Layout& AmpFace::layoutFor (const juce::String& body)
{
    static const Layout tall  { 22, 44, 556, 138, true, 22, 556, 194, 214, { 0, 24, 600, 406 }, false };
    static const Layout thin  { 22, 44, 556, 72, true, 22, 556, 128, 280, { 0, 24, 600, 406 }, false };
    static const Layout head  { 30, 128, 540, 240, false, 0, 0, 0, 0, { 0, 96, 600, 304 }, true };
    static const Layout headg { 28, 296, 544, 84, true, 28, 544, 112, 178, { 0, 96, 600, 304 }, true };
    static const Layout top   { 34, 40, 532, 70, true, 40, 520, 172, 228, { 0, 24, 600, 406 }, false };

    if (body == "thin")  return thin;
    if (body == "head")  return head;
    if (body == "headg") return headg;
    if (body == "top")   return top;
    return tall;
}

juce::Colour AmpFace::ruleColour (const AmpLook& look)
{
    return c (printFor (look.panel).rule);
}

juce::Colour AmpFace::printColour (const AmpPreset& preset, const AmpPart& p)
{
    const auto cx = (float) p.x + (float) p.width() * 0.5f, cy = (float) p.y + (float) p.height() * 0.5f;
    auto colour = printFor (preset.look.panel).print;

    for (const auto& q : preset.parts)
    {
        if (q.type == "section" && cx >= (float) q.x && cx <= (float) (q.x + q.w) && cy >= (float) q.y && cy <= (float) (q.y + q.h))
            colour = q.mat == "black" ? amp::printLight : (q.mat == "orange" || q.mat == "white") ? amp::printDark : amp::printOnMetal;
    }

    return c (colour);
}

void AmpFace::paintBackground (juce::Graphics& g, const AmpPreset& preset)
{
    const auto& look = preset.look;
    const auto& lay = layoutFor (look.body);
    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) amp::faceW, (float) amp::faceH);

    drawImageInto (g, ampArt::get ("body_" + look.body + "_" + look.tolex + "_" + look.piping + ".webp"), full);
    drawImageInto (g, ampArt::get ("panel_" + look.body + "_" + look.panel + ".webp"),
                   juce::Rectangle<float> ((float) lay.px, (float) lay.py, (float) lay.pw, (float) lay.panelH));

    if (lay.grille)
        drawImageInto (g, ampArt::get ("grille_" + look.body + "_" + look.grille + ".webp"),
                       juce::Rectangle<float> ((float) lay.gx, (float) lay.grilleY, (float) lay.gw, (float) lay.grilleH));

    for (const auto& p : preset.parts)
        if (p.type == "section")
            paintSection (g, preset, p);

    if (lay.feet)
    {
        for (const auto x : { amp::footLeftX, amp::footRightX })
        {
            const auto foot = juce::Rectangle<float> ((float) x, (float) amp::footY, (float) amp::footW, (float) amp::footH);
            juce::Path shape;
            shape.addRoundedRectangle (foot.getX(), foot.getY(), foot.getWidth(), foot.getHeight(), (float) amp::footRadius, (float) amp::footRadius,
                                       false, false, true, true);
            g.setGradientFill (linear (0.0f, foot.getY(), 0.0f, foot.getBottom(), { { 0.0, amp::footTop }, { 1.0, amp::footBottom } }));
            g.fillPath (shape);
            g.setColour (c (amp::footShine));
            g.fillRect (foot.withHeight (1.0f));
        }
    }

    if (look.corners == "chrome" || look.corners == "black")
    {
        const auto b = lay.body;
        const auto size = (float) amp::cornerSize;
        const std::pair<const char*, juce::Point<int>> corners[] {
            { "tl", { b.getX(), b.getY() } }, { "tr", { b.getRight() - amp::cornerSize, b.getY() } },
            { "bl", { b.getX(), b.getBottom() - amp::cornerSize } }, { "br", { b.getRight() - amp::cornerSize, b.getBottom() - amp::cornerSize } },
        };

        for (const auto& [name, at] : corners)
            drawImageInto (g, ampArt::get ("corner_" + look.corners + "_" + name + ".webp"), { (float) at.x, (float) at.y, size, size });
    }

    if (look.studs)
    {
        const auto b = lay.body;
        const auto stud = [&g] (int x, int y)
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) amp::studSize, (float) amp::studSize);
            const auto circle = ellipse (r);
            circleShadow (g, r, amp::plateRimShadow, 1.0f, 1.0f);
            g.setGradientFill (radial (r, 0.35f, 0.3f, { { 0.0, amp::studLight }, { 0.7, amp::studMid }, { 1.0, amp::studDark } }));
            g.fillPath (circle);
        };

        for (int x = 40; x <= 560; x += amp::studPitch)
        {
            if (x < 222 || x > 378)
                stud (x, b.getY() + 5);

            stud (x, b.getBottom() - 12);
        }

        for (int y = b.getY() + 32; y <= b.getBottom() - 34; y += amp::studPitch)
        {
            stud (7, y);
            stud (588, y);
        }
    }
}

void AmpFace::paintPrint (juce::Graphics& g, const AmpPreset& preset, const AmpPart& p)
{
    const auto pc = printColour (preset, p);

    if (p.isKnob())
        paintKnobPrint (g, p, pc);
    else if (p.isToggle())
        paintSwitchPrint (g, p, pc);
    else if (! p.isLight() && p.type != "section")
        paintPrintOnly (g, preset, p, pc);
}

void AmpFace::paintBody (juce::Graphics& g, const AmpPreset&, const AmpPart& p, float value, bool lit)
{
    if (p.isKnob())
        paintKnobBody (g, p, juce::jlimit (0.0f, 1.0f, value));
    else if (p.isToggle())
        paintSwitchBody (g, p, value >= 0.5f);
    else if (p.isLight())
        paintLightBody (g, p, lit);
}

void AmpFace::paintFace (juce::Graphics& g, const AmpPreset& preset, Layer layer)
{
    paintBackground (g, preset);

    for (const auto& p : preset.parts)
    {
        if (p.type == "section")
            continue;

        paintPrint (g, preset, p);

        if (! (p.isKnob() || p.isToggle() || p.isLight()))
            continue;

        if (layer == Layer::staticOnly && isLive (p))
            continue;   // drawn on top, live

        // A driving switch shows up (out), a light that follows the block lit, as the builder shows them.
        const auto value = p.isToggle() && ! p.isFixed() ? 1.0f : p.value;
        paintBody (g, preset, p, value, p.lightsWhileOn());
    }
}

juce::Image AmpFace::compose (const AmpPreset& preset, Layer layer, float scale)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto key = ampPreset::toJson (preset) + (layer == Layer::staticOnly ? "#static" : "#all") + "#" + juce::String (scale, 2);
    auto& cache = faceCache();
    static juce::uint32 clock = 0;

    if (auto found = cache.find (key); found != cache.end())
    {
        found->second.lastUsed = ++clock;
        return found->second.image;
    }

    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) amp::faceW * scale), juce::roundToInt ((float) amp::faceH * scale), true);

    {
        juce::Graphics g (image);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.addTransform (juce::AffineTransform::scale (scale));
        paintFace (g, preset, layer);
    }

    if (cache.size() >= maxCachedFaces)
    {
        auto oldest = cache.begin();

        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->second.lastUsed < oldest->second.lastUsed)
                oldest = it;

        cache.erase (oldest);
    }

    cache[std::move (key)] = { image, ++clock };
    return image;
}

void AmpFace::paintKnobPicture (juce::Graphics& g, const juce::String& style, juce::Rectangle<float> r)
{
    paintKnobCap (g, style, r, 20);   // value 0.5 of the 41 frames
}

void AmpFace::clearCache()  { faceCache().clear(); }
int AmpFace::numCached()    { return (int) faceCache().size(); }

juce::Rectangle<int> AmpFace::partBounds (const AmpPart& p)
{
    auto r = juce::Rectangle<int> (p.x, p.y, p.width(), p.height());

    if (p.isKnob())
    {
        const auto kp = knobPrint (p);
        const auto ring = kp.numbers > 0 ? juce::roundToInt (kp.numberPx * 1.6f + 4.0f) : kp.ticks ? 8 : 0;
        r = r.expanded (ring);

        if (p.label.isNotEmpty())
        {
            const auto h = juce::roundToInt (kp.labelPx * 1.15f * (float) numLines (p.label)) + 12;
            r = r.getUnion (p.labelPos == "above" ? juce::Rectangle<int> (p.x - 24, p.y - h, p.width() + 48, h)
                                                  : juce::Rectangle<int> (p.x - 24, p.y + p.height(), p.width() + 48, h));
        }
    }
    else if (p.isToggle())
    {
        const auto lineH = juce::roundToInt ((p.h < 40 ? 6.5f : 9.5f) * 1.15f);

        if (p.top.isNotEmpty())
            r = r.getUnion ({ p.x - 24, p.y - 3 - lineH * numLines (p.top), p.w + 48, lineH * numLines (p.top) + 3 });

        if (p.label.isNotEmpty())
            r = r.getUnion ({ p.x - 24, p.y + p.h, p.w + 48, lineH * numLines (p.label) + 3 });
    }

    return r;
}

//==============================================================================
AmpFace::AmpFace()
{
    setInterceptsMouseClicks (false, false);
}

void AmpFace::setFace (const AmpPreset& preset, Layer layer)
{
    image = compose (preset, layer);
    repaint();
}

void AmpFace::paint (juce::Graphics& g)
{
    if (image.isValid())
        g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
}

} // namespace encore::kit
