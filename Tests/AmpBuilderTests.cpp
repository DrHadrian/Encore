// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "UiSnapshot.h"

#include "ui/kit/AmpArt.h"

namespace encore::test
{

/** The amp builder (design/AMP-BUILDER.md): its art, its data, the face, the
    Edit view and the builder. */
class AmpBuilderTests final : public juce::UnitTest
{
public:
    AmpBuilderTests() : juce::UnitTest ("Amp builder", "Encore") {}

    void runTest() override
    {
        beginTest ("Art: every layer and sprite decodes at its 2x size, once");
        {
            namespace art = kit::ampArt;
            const auto all = art::names();
            expectEquals (all.size(), 227 + 8, "227 WebP layers and 8 sprites");

            // The layouts' recesses (AMP-BUILDER-LAYERS.md), in face pixels x 2.
            const std::pair<const char*, juce::Point<int>> sizes[] {
                { "body_", { 1200, 860 } },       { "corner_", { 44, 44 } },
                { "panel_tall_", { 1112, 276 } }, { "panel_thin_", { 1112, 144 } }, { "panel_head_", { 1080, 480 } },
                { "panel_top_", { 1064, 140 } },  { "panel_headg_", { 1088, 168 } },
                { "grille_tall_", { 1112, 428 } }, { "grille_thin_", { 1112, 560 } }, { "grille_top_", { 1040, 456 } },
                { "grille_headg_", { 1088, 356 } },
            };

            int checked = 0;

            for (const auto& name : all)
            {
                const auto image = art::get (name);
                expect (image.isValid(), name + " decodes");

                for (const auto& [prefix, size] : sizes)
                {
                    if (name.startsWith (prefix))
                    {
                        expectEquals (image.getWidth(), size.x, name);
                        expectEquals (image.getHeight(), size.y, name);
                        ++checked;
                        break;
                    }
                }
            }

            expectEquals (checked, 227, "every layer has the size of its layout");

            // Layers keep their transparency (the body's panel and grille are cut out).
            const auto body = art::get ("body_tall_black_gold.webp");
            expect (body.hasAlphaChannel());
            expect (body.getPixelAt (600, 200).getAlpha() == 0, "the panel area is cut out of the body");
            expect (body.getPixelAt (10, 600).getAlpha() == 255, "the tolex is solid");

            // Cached: the second ask is the same image; a missing name is a null image.
            expect (art::get ("panel_tall_gold.webp") == art::get ("panel_tall_gold.webp"));
            expect (art::get ("panel_tall_nothing.webp").isNull());
            expect (art::get ("knob_black.png").getWidth() == 160);
            expectEquals (art::get ("chicken_strip.png").getHeight(), 41 * 128, "41 frames, one per knob width");

            // The jewel's colours: the board's hue-rotate filters, made once.
            const auto meanColour = [] (const juce::Image& im)
            {
                double r = 0, g = 0, b = 0, n = 0;

                for (int y = 0; y < im.getHeight(); y += 2)
                    for (int x = 0; x < im.getWidth(); x += 2)
                        if (const auto c = im.getPixelAt (x, y); c.getAlpha() > 200)
                        {
                            r += c.getFloatRed(); g += c.getFloatGreen(); b += c.getFloatBlue(); n += 1;
                        }

                return juce::Colour::fromFloatRGBA ((float) (r / n), (float) (g / n), (float) (b / n), 1.0f);
            };

            const auto red = meanColour (art::jewel ("red", true));
            const auto green = meanColour (art::jewel ("green", true));
            const auto blue = meanColour (art::jewel ("blue", true));
            const auto amber = meanColour (art::jewel ("amber", true));
            expect (red.getRed() > red.getGreen() && red.getRed() > red.getBlue(), "red is red");
            expect (green.getGreen() > green.getRed() && green.getGreen() > green.getBlue(), "green is green");
            expect (blue.getBlue() > blue.getRed() && blue.getBlue() > blue.getGreen(), "blue is blue");
            expect (amber.getRed() > amber.getBlue() && amber.getGreen() > green.getRed() * 0.5f, "amber is warm");
            expect (art::jewel ("green", true) == art::jewel ("green", true), "made once");
            expect (art::jewel ("green", false) == art::get ("jewel_off.png"), "unlit: jewel_off");

            art::clearCache();
            expectEquals (art::numCached(), 0);
        }
    }
};

static AmpBuilderTests ampBuilderTests;

} // namespace encore::test
