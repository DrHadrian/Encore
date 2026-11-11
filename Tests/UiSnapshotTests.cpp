// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "UiSnapshot.h"

#include "ui/theme/EncoreFonts.h"
#include "ui/theme/EncoreLookAndFeel.h"
#include "ui/theme/Icons.h"

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
    }
};

static UiSnapshotTests uiSnapshotTests;

} // namespace encore::test
