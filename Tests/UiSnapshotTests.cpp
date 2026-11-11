// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include <cmath>

#include "UiSnapshot.h"
#include "KitGallery.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"
#include "ui/kit/MeterBar.h"
#include "ui/kit/NoticeBar.h"
#include "ui/kit/Lcd.h"

namespace encore::test
{

/** The design system in the app (docs/design): fonts sized like the
    reference boards, icons and logo loaded, the look on every control.
    Screens are rendered off-screen to PNGs with the boards' names when
    ENCORE_UI_SNAPSHOTS is set, for docs/design/tools/ui-compare.py. */
class UiSnapshotTests final : public juce::UnitTest
{
public:
    UiSnapshotTests() : juce::UnitTest ("UI design system", "Encore") {}

    static juce::Rectangle<int> inkBounds (const juce::Image& image)
    {
        juce::Rectangle<int> ink;
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (data.getPixelColour (x, y).getBrightness() > 0.55f)
                    ink = ink.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1) : ink.getUnion ({ x, y, 1, 1 });

        return ink;
    }

    void runTest() override
    {
        const ScopedEncoreLook look;

        beginTest ("Font calibration: text is as big as on the reference boards");
        {
            // edit.png: "Cue 1 of 8: Clean", 14 px semibold, ink 112 x 11 px.
            juce::Image image (juce::Image::ARGB, 200, 40, true);
            {
                juce::Graphics g (image);
                g.fillAll (juce::Colours::black);
                g.setColour (juce::Colours::white);
                g.setFont (theme::fontOfSize (theme::type::Weight::semibold, 14.0f));
                g.drawText ("Cue 1 of 8: Clean", 4, 4, 196, 30, juce::Justification::centredLeft);
            }

            const auto ink = inkBounds (image);
            expectWithinAbsoluteError (ink.getWidth(), 112, 3, "width matches the board");
            expectWithinAbsoluteError (ink.getHeight(), 11, 1, "cap height matches the board");
        }

        beginTest ("Every icon and the logo load");
        {
            for (int i = 0; i <= (int) theme::Icon::triangleLeft; ++i)
            {
                const auto bounds = theme::iconPath ((theme::Icon) i).getBounds();
                expect (! bounds.isEmpty(), "icon " + juce::String (i));
                expect (juce::Rectangle<float> (-1.0f, -1.0f, 26.0f, 26.0f).contains (bounds), "icon " + juce::String (i) + " in its 24 box");
            }

            expectWithinAbsoluteError (theme::logoAspect(), 98.0f / 17.0f, 0.3f);

            // Drawn into its 98 x 17 box, the ink stays inside it.
            juce::Image logo (juce::Image::ARGB, 120, 60, true);
            {
                juce::Graphics g (logo);
                theme::drawLogo (g, { 10.0f, 20.0f, 98.0f, 17.0f });
            }
            int top = logo.getHeight(), bottom = -1;

            for (int y = 0; y < logo.getHeight(); ++y)
                for (int x = 0; x < logo.getWidth(); ++x)
                    if (logo.getPixelAt (x, y).getAlpha() > 64)
                    {
                        top = juce::jmin (top, y);
                        bottom = juce::jmax (bottom, y);
                    }

            expect (top >= 19 && bottom <= 38, "logo ink inside its box: rows " + juce::String (top) + ".." + juce::String (bottom));
        }

        beginTest ("Inter everywhere, also for fonts made without a typeface");
        {
            theme::EncoreLookAndFeel lnf;
            const juce::Font plain (juce::FontOptions (14.0f));
            expect (lnf.getTypefaceForFont (plain)->getName().containsIgnoreCase ("Inter"));
        }

        beginTest ("Menu shortcuts are split off the item text");
        {
            using LnF = theme::EncoreLookAndFeel;
            expect (LnF::splitShortcut ("Save now (Ctrl+S)") == std::pair<juce::String, juce::String> ("Save now", "Ctrl+S"));
            expect (LnF::splitShortcut ("Pre-show check... (F9)").second == "F9");
            expect (LnF::splitShortcut ("Go to this cue (G)").second == "G");
            expect (LnF::splitShortcut ("Stage settings (this show)").second.isEmpty());
            expect (LnF::splitShortcut ("Save a copy to... (USB stick, backup)").second.isEmpty());
        }

        beginTest ("Kit: every component draws, at 1x and 2x");
        {
            KitGallery gallery;
            gallery.setBounds (0, 0, 1440, 1320);
            const auto image = renderSnapshot (gallery, "kit");
            expect (hasContent (image), "the gallery drew something");

            const auto hiDpi = gallery.createComponentSnapshot (gallery.getLocalBounds(), false, 2.0f);
            expectEquals (hiDpi.getWidth(), 2880);
            expect (hasContent (hiDpi));
        }

        beginTest ("Kit: meters map the scale as labelled and only move when the level does");
        {
            using kit::MeterBar;
            expectWithinAbsoluteError (MeterBar::toProportion (-60.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (MeterBar::toProportion (-12.0f), 4.0f / 6.0f, 1.0e-6f);
            expectWithinAbsoluteError (MeterBar::toProportion (-6.0f), 5.0f / 6.0f, 1.0e-6f);
            expectWithinAbsoluteError (MeterBar::toProportion (0.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (MeterBar::toProportion (6.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (MeterBar::toProportion (-200.0f), 0.0f, 1.0e-6f);

            float last = -1.0f;

            for (float db = -70.0f; db <= 3.0f; db += 0.5f)
            {
                const auto p = MeterBar::toProportion (db);
                expect (p >= last, "the scale never runs backwards");
                last = p;
            }

            MeterBar meter ("Input");
            meter.setBounds (0, 0, 400, 14);

            for (int i = 0; i < 200; ++i)   // silence, then a burst, then silence again: never NaN, never stuck
            {
                meter.setPeak (i < 50 || i > 60 ? 0.0f : 0.8f);

                if (i == 60)   // the burst lights the bar
                    expect (hasContent (meter.createComponentSnapshot (meter.getLocalBounds())), "a level shows");
            }

            expect (std::isfinite (meter.getDisplayDb()), "never NaN");
            expect (MeterBar::toProportion (meter.getDisplayDb()) == 0.0f, "falls back to the bottom after the burst");
        }

        beginTest ("Kit: tiny and empty sizes never crash");
        {
            KitGallery gallery;

            for (auto size : { juce::Point<int> (0, 0), juce::Point<int> (1, 1), juce::Point<int> (640, 480), juce::Point<int> (3840, 2160) })
            {
                gallery.setSize (size.x, size.y);

                for (auto* child : gallery.getChildren())
                {
                    child->setSize (juce::jmin (child->getWidth(), 3), juce::jmin (child->getHeight(), 3));

                    if (child->getWidth() > 0 && child->getHeight() > 0)
                        child->createComponentSnapshot (child->getLocalBounds());
                }
            }

            kit::NoticeBar bar (kit::NoticeBar::Kind::lost);
            bar.setText (juce::String::repeatedString ("very long text ", 200), juce::String::repeatedString ("note ", 100));
            bar.setBounds (0, 0, 120, 35);

            for (int i = 0; i < 40; ++i)
                bar.advance();

            bar.createComponentSnapshot (bar.getLocalBounds());   // long text is cut, not wrapped over the bar

            kit::Lcd lcd (true);
            lcd.setItems ({});
            expectEquals (lcd.getIdealWidth(), 2 * (theme::size::lcdPadXStage + 1));
            lcd.setItems ({ { juce::String::repeatedString ("W", 300), "Song", 0 } });
            lcd.setBounds (0, 0, 50, kit::Lcd::heightFor (true));
            lcd.createComponentSnapshot (lcd.getLocalBounds());
        }
    }
};

static UiSnapshotTests uiSnapshotTests;

} // namespace encore::test
