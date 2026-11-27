// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#include "FakeDevice.h"
#include "ui/theme/EncoreAmpTheme.h"
#include "TestSignals.h"
#include "UiSnapshot.h"

#include "engine/blocks/NamAmpBlock.h"
#include "model/ShowJson.h"
#include "session/AmpLibrary.h"
#include "session/ShowSession.h"
#include "ui/kit/AmpArt.h"
#include "ui/kit/AmpFace.h"
#include "ui/kit/AmpFaceView.h"
#include "ui/AmpBuilderView.h"
#include "ui/BlockPanel.h"
#include "ui/kit/AmpTemplates.h"

namespace encore::test
{

/** The amp builder (design/AMP-BUILDER.md): its art, its data, the face, the
    Edit view and the builder. */
class AmpBuilderTests final : public juce::UnitTest
{
public:
    AmpBuilderTests() : juce::UnitTest ("Amp builder", "Encore") {}

    /** One of every part type, with fields a newer Encore might add. */
    static AmpPreset sampleAmp()
    {
        AmpPreset amp;
        amp.id = "amp-sample";
        amp.name = "Sample 30";
        amp.capture = "models/Plexi 59 crunch.nam";
        amp.look.body = "head";
        amp.look.tolex = "orange";
        amp.look.piping = "none";
        amp.look.panel = "white";
        amp.look.corners = "black";
        amp.look.studs = true;

        const auto part = [] (const char* id, const char* type, int x, int y, int w, int h)
        {
            AmpPart p;
            p.id = id; p.type = type; p.x = x; p.y = y; p.w = w; p.h = h;
            return p;
        };

        auto knob = part ("k1", "knob", 56, 72, 0, 0);
        knob.style = "chicken"; knob.size = "XL"; knob.drives = "input"; knob.value = 0.6f; knob.label = "INPUT"; knob.labelPos = "below"; knob.scale = "ticks";
        auto fixedKnob = part ("k2", "knob", 130, 72, 0, 0);
        fixedKnob.style = "plain"; fixedKnob.size = "S"; fixedKnob.drives = "fixed"; fixedKnob.value = 0.65f; fixedKnob.scale = "n12";
        auto toggle = part ("t1", "toggle", 398, 66, 40, 70);
        toggle.switchStyle = "push"; toggle.drives = "levelMatch"; toggle.value = 1.0f; toggle.top = "OFF"; toggle.label = "ON";
        auto light = part ("j1", "jewel", 544, 90, 28, 28);
        light.style = "led"; light.colour = "green"; light.lights = "off";
        auto section = part ("s1", "section", 28, 50, 156, 60);  section.mat = "gold";
        auto text = part ("tt", "text", 286, 156, 196, 24);     text.text = "SUPER CRUSH 100"; text.font = "serif";
        auto plate = part ("n1", "plate", 225, 170, 150, 60);   plate.text = ""; plate.mat = "blackgold"; plate.shape = "oval";
        auto logo = part ("l1", "logo", 52, 214, 190, 34);      logo.colour = "cream"; logo.logoSize = 34;
        auto symbol = part ("y1", "symbol", 68, 242, 14, 14);   symbol.icon = "power";
        auto trim = part ("r1", "trim", 22, 133, 556, 5);       trim.mat = "silver";
        auto title = part ("a1", "title", 36, 50, 104, 12);     title.text = "PREAMP";

        knob.extra = juce::JSON::parse (R"({"glow": 2})");   // from a newer Encore
        amp.parts = { section, title, knob, fixedKnob, toggle, light, text, plate, logo, symbol, trim,
                      part ("o1", "outline", 60, 138, 480, 220), part ("bg", "badge", 494, 144, 36, 42), part ("h1", "handle", 40, 142, 12, 212),
                      part ("i1", "jack", 34, 62, 16, 16), part ("w1", "screw", 28, 160, 6, 6) };
        amp.look.extra = juce::JSON::parse (R"({"tolexGrain": "fine"})");
        amp.extra = juce::JSON::parse (R"({"captures": ["a.nam", "b.nam"]})");
        return amp;
    }

    static void playOneBlock (AudioEngine& engine)
    {
        constexpr int samples = 256;
        std::vector<float> in ((size_t) samples, 0.0f), outL ((size_t) samples), outR ((size_t) samples);
        const float* inputs[] = { in.data() };
        float* outputs[] = { outL.data(), outR.data() };
        engine.audioDeviceIOCallbackWithContext (inputs, 1, outputs, 2, samples, {});
    }

    static void pumpUntil (AudioEngine& engine, std::function<bool()> done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (! done() && juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            playOneBlock (engine);
            engine.collectGarbage();
        }
    }

    void runTest() override
    {
        ScopedEncoreLook look;

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

            for (const auto& file : all)
            {
                const auto image = art::get (file);
                expect (image.isValid(), file + " decodes");

                for (const auto& [prefix, size] : sizes)
                {
                    if (file.startsWith (prefix))
                    {
                        expectEquals (image.getWidth(), size.x, file);
                        expectEquals (image.getHeight(), size.y, file);
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
            const auto unlit = meanColour (art::jewel ("green", false));
            expect (unlit.getBrightness() < green.getBrightness() * 0.6f, "unlit: darker");
            expect (art::jewel ("red", false) != art::jewel ("red", true));

            art::clearCache();
            expectEquals (art::numCached(), 0);
        }

        beginTest ("Templates: the eight templates and the five example amps, as on the board");
        {
            namespace tpl = kit::ampTemplates;
            juce::StringArray names;

            for (const auto& t : tpl::all())
                names.add (t.name);

            expectEquals (names.joinIntoString (", "), juce::String ("British gold, Black panel, Plexi, Cream, Tweed top, Grille head, Framed head, Jazz clean"));
            juce::StringArray amps;

            for (const auto& e : tpl::examples())
                amps.add (e.amp.name);

            expectEquals (amps.joinIntoString (", "), juce::String ("Encore Gold 30, JC-120, Super Crush 100, Blues Junior, 5150 III 50W"));
            expectEquals (tpl::defaultFace().name, juce::String ("Encore Gold 30"));

            for (const auto& e : tpl::examples())
            {
                juce::String why;
                expect (e.amp.canSave (&why), e.amp.name + ": " + why);
                expect (e.amp.capture.isEmpty(), "no capture is shipped");
            }

            // Encore Gold 30: every control drives a real parameter (it stands in for any amp block without its own).
            int driving = 0;

            for (const auto& p : tpl::defaultFace().parts)
            {
                expect (! p.isFixed(), p.id + " isn't fixed");

                if (p.drivesParameter())
                    ++driving;
            }

            expectEquals (driving, 6, "Input, Bass, Mid, Treble, Output and Level match");

            // The board's bindings on the others (AMP-BUILDER.md: example amps).
            const auto count = [] (const AmpPreset& a, const char* drives)
            {
                return (int) std::count_if (a.parts.begin(), a.parts.end(), [drives] (const AmpPart& p) { return p.drives == drives; });
            };

            const auto& jc = tpl::examples()[1].amp;
            expectEquals (count (jc, "fixed"), 8, "JC-120: channel 1 (4 knobs), Distortion, Reverb, Speed, Depth fixed");
            expectEquals (count (jc, "input") + count (jc, "treble") + count (jc, "mid") + count (jc, "bass") + count (jc, "output"), 5);

            const auto made = tpl::makeFrom ("frame", "My head");
            expect (made.id.isNotEmpty() && made.id != tpl::find ("frame")->amp.id && made.name == "My head");
            expect (made.parts == tpl::find ("frame")->amp.parts);
        }

        beginTest ("Face: composed once to a 2x image, cached by content, the live parts left for the view");
        {
            kit::AmpFace::clearCache();
            const auto& gold = kit::ampTemplates::defaultFace();
            const auto all = kit::AmpFace::compose (gold, kit::AmpFace::Layer::everything);
            expectEquals (all.getWidth(), 1200);
            expectEquals (all.getHeight(), 860);
            expect (all == kit::AmpFace::compose (gold, kit::AmpFace::Layer::everything), "the second ask is the cached image");
            expectEquals (kit::AmpFace::numCached(), 1);

            const auto still = kit::AmpFace::compose (gold, kit::AmpFace::Layer::staticOnly);
            expect (still != all);

            // The knobs' place differs between the two (their bodies are drawn live on top).
            const auto knob = *gold.findPart ("k1");
            const auto centre = juce::Point<int> ((knob.x + knob.width() / 2) * 2, (knob.y + knob.height() / 2) * 2);
            expect (all.getPixelAt (centre.x, centre.y) != still.getPixelAt (centre.x, centre.y), "a driving knob isn't in the static image");

            // The print stays in it: a label under the knob.
            const auto plate = *gold.findPart ("p1");
            expect (all.getPixelAt ((plate.x + 4) * 2, (plate.y + plate.h / 2) * 2) == still.getPixelAt ((plate.x + 4) * 2, (plate.y + plate.h / 2) * 2));

            // Another amp is another image; an edited amp too.
            auto edited = gold;
            edited.look.tolex = "cream";
            expect (kit::AmpFace::compose (edited, kit::AmpFace::Layer::everything) != all);

            // Every example and template composes, at 1x and 2x, every part type drawn.
            for (const auto& e : kit::ampTemplates::examples())
            {
                const auto face = kit::AmpFace::compose (e.amp, kit::AmpFace::Layer::everything);
                expect (face.isValid() && face.getPixelAt (600, 500).getAlpha() > 0, e.amp.name);
                writeSnapshot (face, "amp-face-" + e.amp.id.fromFirstOccurrenceOf ("example-", false, false));
            }

            for (const auto& t : kit::ampTemplates::all())
                expect (kit::AmpFace::compose (t.amp, kit::AmpFace::Layer::staticOnly, 1.0f).getWidth() == 600, t.name);

            // Unknown materials and part types fall back, never crash.
            auto odd = sampleAmp();
            odd.look.tolex = "plaid";
            odd.parts.push_back ({});
            odd.parts.back().type = "hologram";
            expect (kit::AmpFace::compose (odd, kit::AmpFace::Layer::everything).isValid());

            // The component shows the composed face, scaled.
            kit::AmpFace view;
            view.setFace (gold);
            view.setBounds (0, 0, 600, 430);
            expect (view.getImage() == all);
            expect (hasContent (view.createComponentSnapshot (view.getLocalBounds())));

            kit::AmpFace::clearCache();
            expectEquals (kit::AmpFace::numCached(), 0);
        }

        beginTest ("Edit: an amp block shows a face; its driving controls are real, Fixed ones only say so, lights follow the block");
        {
            NamAmpBlock block;
            BlockPanel panel (block);
            panel.setBounds (0, 0, 1100, 640);
            auto* face = panel.getAmpFace();
            expect (face != nullptr, "no placeholder: Encore Gold 30 when the block has no amp of its own");

            if (face == nullptr)
                return;

            expectEquals (face->getAmp().name, juce::String ("Encore Gold 30"));
            expectEquals (face->getWidth(), 600);
            expectEquals (face->getHeight(), 430);
            expectEquals (face->getDrivenParameters().joinIntoString (","), juce::String ("input,bass,mid,treble,output,normalise"));

            // A knob drives its parameter, both ways.
            auto* treble = face->findKnob ("treble");
            expect (treble != nullptr);
            treble->setValue (4.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (block.findParameter ("treble")->getValue(), 4.0f, 0.01f, "turning the face's knob sets the block");
            block.findParameter ("treble")->setValue (-3.0f);
            panel.refresh();
            expectWithinAbsoluteError ((float) treble->getValue(), -3.0f, 0.01f, "a change elsewhere (MIDI) shows on the face");
            expectWithinAbsoluteError ((float) treble->getDoubleClickReturnValue(), block.findParameter ("treble")->getSpec().defaultValue, 1.0e-6f,
                                       "double-click: the default");

            // A knob a pedal moves shows "EXP 1" on the face too, away from its label.
            panel.getExpressionPedal = [] (const juce::String& id) { return id == "treble" ? 1 : 0; };
            panel.refresh();
            expectEquals (face->getChip ("treble"), juce::String ("EXP 1"));
            expect (face->getChip ("bass").isEmpty());
            renderSnapshot (panel, "v32-amp-face-exp");
            panel.getExpressionPedal = [] (const juce::String&) { return 0; };
            panel.refresh();
            expect (face->getChip ("treble").isEmpty(), "gone when the pedal is taken off");

            // Level match is the switch.
            auto* match = face->findSwitch ("normalise");
            expect (match != nullptr);
            const auto before = block.findParameter ("normalise")->getValue();
            match->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            expect (std::abs (block.findParameter ("normalise")->getValue() - before) > 0.5f, "the switch sets Level match");

            // The pilot light follows the block, and turns it off and on.
            expect (block.isEnabled() && face->isOn());
            face->clickPower();
            expect (! block.isEnabled() && ! face->isOn(), "clicking the light turns the amp off");
            face->clickPower();
            expect (block.isEnabled() && face->isOn());

            const auto jewel = *face->getAmp().findPart ("j1");
            expectEquals (face->getTooltipAt ({ jewel.x + 4, jewel.y + 4 }), juce::String ("Turn the block on or off"));
            expect (hasContent (panel.createComponentSnapshot (panel.getLocalBounds())));
            renderSnapshot (panel, "v32-amp-face-edit");

            // An amp of its own: JC-120, with Fixed knobs that only say so.
            const auto& jc = kit::ampTemplates::examples()[1].amp;
            panel.setAmpFace (jc);
            face = panel.getAmpFace();
            expectEquals (face->getAmp().name, juce::String ("JC-120"));
            const auto fixed = *jc.findPart ("d6");   // REVERB, fixed at 3
            expectEquals (face->getTooltipAt ({ fixed.x + 4, fixed.y + 4 }), juce::String ("Fixed at 3 in this capture"));
            expect (face->findKnob ("input") != nullptr && face->findKnob ("input")->getTitle() == "VOLUME");
            expect (face->findSwitch ("normalise") != nullptr, "the JC's toggle drives Level match");
            panel.getExpressionPedal = [] (const juce::String& id) { return id == "input" ? 2 : 0; };
            panel.refresh();
            panel.setAmpFace (jc);   // a new face keeps showing the pedal
            face = panel.getAmpFace();
            expectEquals (face->getChip ("input"), juce::String ("EXP 2"));
            panel.getExpressionPedal = nullptr;

            // The 5150's channel LEDs: the green one is always off, the red one follows.
            const auto& head = kit::ampTemplates::examples()[4].amp;
            kit::AmpFaceView view (head);
            const auto green = *head.findPart ("e1");
            expectEquals (view.getTooltipAt ({ green.x + 2, green.y + 2 }), juce::String ("Always off: it was off when the capture was made"));
            const auto fixedSwitch = *head.findPart ("t1");
            expectEquals (view.getTooltipAt ({ fixedSwitch.x + 2, fixedSwitch.y + 2 }), juce::String ("Fixed out in this capture"));

            // A face whose lights are all always off gets the power LED.
            auto dark = kit::ampTemplates::defaultFace();
            dark.parts.erase (std::remove_if (dark.parts.begin(), dark.parts.end(), [] (const AmpPart& p) { return p.isLight(); }), dark.parts.end());
            kit::AmpFaceView noLight (dark);
            bool powered = false;
            noLight.onPowerClicked = [&] { powered = true; };
            noLight.clickPower();
            expect (powered, "the power LED stands in");
        }

        beginTest ("Builder: Parts | Face | Inspector; add, NOT SET blocks Save amp, saving writes the show and the library");
        {
            FakeDevice device;
            AudioEngine engine;
            engine.audioDeviceAboutToStart (&device);
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("encore-builder-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            temp.createDirectory();

            {
                ShowSession session (engine);
                expect (session.newShow (temp.getChildFile ("Builder.encore"), "Builder").wasOk());
                pumpUntil (engine, [&] { return ! session.isBuilding(); });
                AmpLibrary library (temp.getChildFile ("Amps"));

                AmpBuilderView builder (session, library);
                builder.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight - 44);
                expectEquals (builder.getAmp().name, juce::String ("Encore Gold 30"));
                builder.select ("k4");   // as the board shows it
                expect (hasContent (renderSnapshot (builder, "v32-amp-builder")));
                expect (builder.countText().startsWith ("12 parts"), builder.countText());
                expect (builder.countText().contains ("template: British gold"));

                // A knob from the parts list: NOT SET until it drives something or is Fixed; Save amp waits.
                const auto id = builder.addPart ("knob", "plain", { 300, 300 });
                expect (builder.getAmp().findPart (id) != nullptr && builder.getAmp().findPart (id)->isNotSet());
                expect (! builder.getSaveButton().isEnabled(), "Save amp is blocked while a knob is NOT SET");
                expect (builder.getSaveButton().getTooltip().contains ("NOT SET"));
                expect (! builder.save());
                expect (session.getAmps().empty(), "nothing saved");
                renderSnapshot (builder, "v32-amp-builder-not-set");

                builder.changeSelected ([] (AmpPart& p) { p.drives = "fixed"; p.value = 0.3f; });
                expect (builder.getSaveButton().isEnabled());
                expect (builder.save());
                expectEquals ((int) session.getAmps().size(), 1);
                expect (library.load (builder.getAmp().id).has_value(), "a copy in the library");
                expect (! builder.hasUnsavedChanges());

                // The 2 px grid; inside the face (below the top 24).
                builder.movePart (id, { 101, 3 });
                const auto* moved = builder.getAmp().findPart (id);
                expect (moved->x % 2 == 0 && moved->y >= 24, "x " + juce::String (moved->x) + ", y " + juce::String (moved->y));

                // Smart guides: a centre within 6 px of another's lines up (magenta guide).
                const auto& k1 = *builder.getAmp().findPart ("k1");
                auto probe = *builder.getAmp().findPart (id);
                const auto wanted = juce::Point<int> (380, k1.y + k1.height() / 2 - probe.height() / 2 + 4);
                const auto landing = builder.landingFor (probe, wanted);
                expectEquals (landing.topLeft.y + probe.height() / 2, k1.y + k1.height() / 2, "centres line up across");
                expect (! landing.guides.empty());

                // Equal spacing: the treble knob near the bass-mid gap snaps to it, both gaps marked.
                const auto& k2 = *builder.getAmp().findPart ("k2");
                const auto& k3 = *builder.getAmp().findPart ("k3");
                const auto gap = k3.x - (k2.x + k2.width());
                auto k4 = *builder.getAmp().findPart ("k4");
                const auto spaced = builder.landingFor (k4, { k3.x + k3.width() + gap + 4, k4.y });
                expectEquals (spaced.topLeft.x, k3.x + k3.width() + gap);
                expectEquals ((int) spaced.gaps.size(), 2);
                expectEquals (spaced.gaps[0].px, gap);

                // Several parts: Arrange.
                builder.select ("k1");
                builder.select ("t1", true);
                expectEquals (builder.getSelection().size(), 2);
                builder.arrange (AmpBuilderView::Arrange::tops);
                expectEquals (builder.getAmp().findPart ("t1")->y, builder.getAmp().findPart ("k1")->y);
                renderSnapshot (builder, "v32-amp-builder-arrange");
                builder.undo();
                expect (builder.getAmp().findPart ("t1")->y != builder.getAmp().findPart ("k1")->y, "Undo");
                builder.redo();
                expectEquals (builder.getAmp().findPart ("t1")->y, builder.getAmp().findPart ("k1")->y, "Redo");

                builder.select ("k1");
                builder.select ("k2", true);
                builder.select ("k3", true);
                builder.changeSelected ([] (AmpPart& p) { if (p.id == "k2") p.x += 10; });
                builder.arrange (AmpBuilderView::Arrange::spaceEvenly);
                const auto* a = builder.getAmp().findPart ("k1");
                const auto* b = builder.getAmp().findPart ("k2");
                const auto* c = builder.getAmp().findPart ("k3");
                expect (std::abs ((b->x - (a->x + a->width())) - (c->x - (b->x + b->width()))) <= 1, "even gaps, the outer two stay");

                // Keyboard: Tab, arrows (2 px, Shift 10), Ctrl+D, Delete, Esc, Ctrl+Z / Ctrl+Y.
                builder.select ({});
                expect (builder.keyPressed (juce::KeyPress (juce::KeyPress::tabKey)));
                expectEquals (builder.getSelection()[0], builder.getAmp().parts.front().id);
                const auto x0 = builder.getAmp().parts.front().x;
                builder.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
                expectEquals (builder.getAmp().parts.front().x, x0 + 2);
                builder.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
                expectEquals (builder.getAmp().parts.front().x, x0 + 12);
                const auto count = builder.getAmp().parts.size();
                builder.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
                expectEquals (builder.getAmp().parts.size(), count + 1);
                builder.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                expectEquals (builder.getAmp().parts.size(), count);
                builder.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
                expectEquals (builder.getAmp().parts.size(), count + 1, "Ctrl+Z brings it back");
                builder.keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0));
                expectEquals (builder.getAmp().parts.size(), count);
                builder.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                expect (builder.getSelection().isEmpty());

                // A template replaces the look and the parts, without asking; Undo brings them back.
                const auto beforeTemplate = builder.getAmp();
                builder.applyTemplate ("frame");
                expectEquals (builder.getAmp().look.body, juce::String ("head"));
                expect (builder.getAmp().parts == kit::ampTemplates::find ("frame")->amp.parts);
                builder.undo();
                expect (builder.getAmp() == beforeTemplate);

                // A part dragged from the list: the drop box shows, then it's added where let go.
                const auto before = builder.getAmp().parts.size();
                builder.dragNewPart ("symbol", {}, { 300, 200 }, false);
                expect (builder.getAmp().parts.size() == before, "not until it's let go");
                builder.dragNewPart ("symbol", {}, { 300, 200 }, true);
                expectEquals (builder.getAmp().parts.size(), before + 1);
                const auto& symbol = builder.getAmp().parts.back();
                expect (symbol.type == "symbol" && std::abs (symbol.x + symbol.w / 2 - 300) <= 8, "centred where it was dropped");
                builder.dragNewPart ("screw", {}, { -40, 900 }, true);
                expectEquals (builder.getAmp().parts.size(), before + 1, "off the face: nothing");

                // Templates: each replaces the look and the parts; the examples are offered from the library.
                for (const auto& t : kit::ampTemplates::all())
                {
                    builder.applyTemplate (t.key);
                    expect (builder.getAmp().look == t.amp.look && builder.getAmp().parts == t.amp.parts, t.name);
                    expect (builder.countText().endsWith ("template: " + t.name), builder.countText());
                    expect (hasContent (renderSnapshot (builder, "v32-amp-builder-template-" + t.key)));
                }

                {
                    auto choices = builder.libraryChoices();
                    juce::StringArray names;

                    for (const auto& choice : choices)
                        names.add (choice.name);

                    for (const auto& e : kit::ampTemplates::examples())
                        expect (names.contains (e.amp.name) == (session.getAmp (e.amp.id) == nullptr),
                                e.amp.name + (session.getAmp (e.amp.id) == nullptr ? " is offered" : " is in the show already (saved above)"));

                    // One saved to the library is offered once, as the library's.
                    expect (library.save (kit::ampTemplates::examples()[2].amp).wasOk());
                    choices = builder.libraryChoices();
                    expectEquals ((int) std::count_if (choices.begin(), choices.end(), [] (const AmpPreset& offered) { return offered.name == "Super Crush 100"; }), 1);

                    // In the show: not offered at all.
                    session.saveAmp (kit::ampTemplates::examples()[3].amp);
                    choices = builder.libraryChoices();
                    expect (std::none_of (choices.begin(), choices.end(), [] (const AmpPreset& offered) { return offered.name == "Blues Junior"; }));
                }

                // A new amp from a template: its own id, the name given, no capture.
                const auto fresh = kit::ampTemplates::makeFrom ("tweed", "Practice combo");
                builder.edit (fresh);
                expectEquals (builder.getAmp().name, juce::String ("Practice combo"));
                expect (builder.getAmp().id != kit::ampTemplates::find ("tweed")->amp.id);
                expect (builder.save(), "a template's knobs are all set: it saves as it is");

                // Every example amp in the builder (the board's five).
                for (const auto& e : kit::ampTemplates::examples())
                {
                    builder.edit (e.amp);
                    expect (hasContent (renderSnapshot (builder, "v32-amp-builder-" + e.amp.id.fromFirstOccurrenceOf ("example-", false, false))));
                }

                // Tiny and huge windows never break.
                for (auto size : { juce::Point<int> (1, 1), juce::Point<int> (640, 400), juce::Point<int> (3840, 2100) })
                {
                    builder.setSize (size.x, size.y);
                    builder.createComponentSnapshot (builder.getLocalBounds().withSizeKeepingCentre (juce::jmin (size.x, 1440), juce::jmin (size.y, 900)));
                }
            }

            engine.audioDeviceStopped();
            temp.deleteRecursively();
        }

        beginTest ("Snapshots: the Edit amp panel with each example amp's face; tiny and huge sizes never break");
        {
            NamAmpBlock block;
            BlockPanel panel (block);
            panel.setBounds (0, 0, 1100, 640);

            for (const auto& e : kit::ampTemplates::examples())
            {
                panel.setAmpFace (e.amp);
                panel.refresh();
                expect (hasContent (renderSnapshot (panel, "v32-amp-face-edit-" + e.amp.id.fromFirstOccurrenceOf ("example-", false, false))));

                // Every driving control is there, every fixed one says so.
                auto* face = panel.getAmpFace();

                for (const auto& p : e.amp.parts)
                {
                    if (p.isKnob() && p.drivesParameter())
                        expect (face->findKnob (ampPreset::parameterFor (p.drives)) != nullptr, e.amp.name + " " + p.id);

                    if (p.isFixed())
                        expectEquals (face->getTooltipAt ({ p.x + p.width() / 2, p.y + p.height() / 2 }), p.fixedTooltip(), e.amp.name + " " + p.id);
                }
            }

            for (auto size : { juce::Point<int> (1, 1), juce::Point<int> (200, 120), juce::Point<int> (3840, 2160) })
            {
                panel.setSize (size.x, size.y);
                panel.createComponentSnapshot (panel.getLocalBounds().withSizeKeepingCentre (juce::jmin (size.x, 1440), juce::jmin (size.y, 900)));
            }

            // The face composes at 1x and 2x for any screen; a face with nothing on it too.
            AmpPreset bare;
            bare.id = "bare";
            expect (kit::AmpFace::compose (bare, kit::AmpFace::Layer::everything, 1.0f).getWidth() == 600);
            kit::AmpFaceView empty (bare);
            empty.setBounds (0, 0, 600, 430);
            expect (hasContent (empty.createComponentSnapshot (empty.getLocalBounds())));
        }

        beginTest ("Data: an amp survives JSON exactly, with the fields a newer Encore added");
        {
            const auto amp = sampleAmp();
            juce::String error;
            const auto text = ampPreset::toJson (amp);
            const auto back = ampPreset::fromJson (text, error);
            expect (back.has_value(), error);

            if (back)
            {
                expect (*back == amp, "every field, in order");
                expect (back->findPart ("k1")->extra.getProperty ("glow", {}).toString() == "2", "a part's unknown field");
                expect (back->look.extra.getProperty ("tolexGrain", {}).toString() == "fine", "the look's unknown field");
                expect (back->extra.getProperty ("captures", {}).isArray(), "the amp's unknown field");
                expect (ampPreset::toJson (*back) == text, "written back as it was read");
            }

            expect (! ampPreset::fromJson ("{\"format\": \"encore-show\"}", error).has_value());
            expect (! ampPreset::fromJson ("not json", error).has_value());
            expect (! ampPreset::fromJson ("{\"format\": \"encore-amp\"}", error).has_value(), "an amp needs an id");

            // Odd values are clamped, never NaN.
            auto odd = ampPreset::fromJson (R"({"format": "encore-amp", "id": "x", "parts": [{"id": "k", "type": "knob", "value": 7, "w": -4}]})", error);
            expect (odd.has_value() && std::abs (odd->parts[0].value - 1.0f) < 1.0e-6f && odd->parts[0].w == 0);
        }

        beginTest ("Data: what each control drives, Fixed and NOT SET, the words");
        {
            expectEquals (ampPreset::parameterFor ("input"), juce::String ("input"));
            expectEquals (ampPreset::parameterFor ("bass"), juce::String ("bass"));
            expectEquals (ampPreset::parameterFor ("mid"), juce::String ("mid"));
            expectEquals (ampPreset::parameterFor ("treble"), juce::String ("treble"));
            expectEquals (ampPreset::parameterFor ("output"), juce::String ("output"));
            expectEquals (ampPreset::parameterFor ("levelMatch"), juce::String ("normalise"));
            expect (ampPreset::parameterFor ("fixed").isEmpty() && ampPreset::parameterFor ({}).isEmpty());

            // Every id is a real NamAmpBlock parameter.
            NamAmpBlock block;

            for (auto d : { "input", "bass", "mid", "treble", "output", "levelMatch" })
                expect (block.findParameter (ampPreset::parameterFor (d)) != nullptr, d);

            expectEquals (ampPreset::tenth (0.6f), juce::String ("6"));
            expectEquals (ampPreset::tenth (0.65f), juce::String ("6.5"));
            expectEquals (ampPreset::tenth (0.0f), juce::String ("0"));
            expectEquals (ampPreset::knobSize ("S"), 26);
            expectEquals (ampPreset::knobSize ("M"), 34);
            expectEquals (ampPreset::knobSize ("L"), 48);
            expectEquals (ampPreset::knobSize ("XL"), 64);

            auto amp = sampleAmp();
            expectEquals (amp.findPart ("k2")->fixedTooltip(), juce::String ("Fixed at 6.5 in this capture"));
            auto sw = *amp.findPart ("t1");
            sw.drives = "fixed";
            expectEquals (sw.fixedTooltip(), juce::String ("Fixed out in this capture"));
            sw.value = 0.0f;
            expectEquals (sw.fixedTooltip(), juce::String ("Fixed pressed in in this capture"));
            sw.switchStyle = "bat";
            expectEquals (sw.fixedTooltip(), juce::String ("Fixed down in this capture"));
            expect (! amp.findPart ("j1")->lightsWhileOn(), "always off");

            juce::String why;
            expect (amp.canSave (&why));
            amp.parts[2].drives = {};
            expect (amp.parts[2].isNotSet());
            expect (! amp.canSave (&why), "a knob that drives nothing and isn't fixed can't be saved");
            expect (why.contains ("NOT SET"));
        }

        beginTest ("Data: the show keeps its amps and which block shows which (schema 4 only when used)");
        {
            auto show = makeDefaultShow ("Amps");
            expect (show.requiredSchemaVersion() < 4, "no amps: older Encore versions keep opening it");

            show.amps.push_back (sampleAmp());
            show.patches[0].mainLane().blocks[0].ampPreset = "amp-sample";
            expectEquals (show.requiredSchemaVersion(), 4);

            juce::String error;
            const auto back = json::fromJson (json::toJson (show), error);
            expect (back.has_value(), error);

            if (back)
            {
                expect (back->amps.size() == 1 && back->amps[0] == show.amps[0], "the amp, exactly");
                expectEquals (back->patches[0].mainLane().blocks[0].ampPreset, juce::String ("amp-sample"));
                expect (*back == show);
            }

            // A newer schema is refused (the user is told to update).
            auto newer = json::toJson (show).replace ("\"schemaVersion\": 4",
                                                      "\"schemaVersion\": " + juce::String (ShowModel::currentSchemaVersion + 1));
            expect (! json::fromJson (newer, error).has_value());
        }

        beginTest ("Data: this computer's amp library, one file per amp");
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("encore-amps-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            AmpLibrary library (folder);
            auto amp = sampleAmp();
            expect (library.save (amp).wasOk());
            expect (library.fileFor (amp.id).existsAsFile());

            auto other = amp;
            other.id = "amp-other";
            other.name = "Another";
            expect (library.save (other).wasOk());

            const auto all = library.loadAll();
            expectEquals ((int) all.size(), 2);
            expectEquals (all[0].name, juce::String ("Another"), "by name");
            expect (library.load (amp.id).value_or (AmpPreset {}) == amp, "unknown fields kept");

            amp.name = "Renamed";
            expect (library.save (amp).wasOk());
            expectEquals (library.load (amp.id)->name, juce::String ("Renamed"), "replaced, not added");
            expectEquals ((int) library.loadAll().size(), 2);

            library.fileFor ("broken").replaceWithText ("{ not json");
            expectEquals ((int) library.loadAll().size(), 2, "a damaged file is skipped");

            expect (library.remove (other.id));
            expectEquals ((int) library.loadAll().size(), 1);

            AmpPreset noId;
            expect (library.save (noId).failed());
            folder.deleteRecursively();
        }

        beginTest ("Data: an amp block shows its amp; its starting settings go live; each patch keeps its own");
        {
            FakeDevice device;
            AudioEngine engine;
            engine.audioDeviceAboutToStart (&device);
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("encore-ampblock-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            temp.createDirectory();

            {
                ShowSession session (engine);
                expect (session.newShow (temp.getChildFile ("Amps.encore"), "Amps").wasOk());
                pumpUntil (engine, [&] { return ! session.isBuilding(); });

                auto amp = sampleAmp();
                amp.parts[3].drives = "output";   // k2 now drives Output, at 0.65
                session.saveAmp (amp);
                expectEquals ((int) session.getAmps().size(), 1);
                session.saveAmp (amp);
                expectEquals ((int) session.getAmps().size(), 1, "saving again replaces it");

                const auto key = session.getActivePatchId();
                expect (session.getPatch (key)->mainLane().blocks[0].type == NamAmpBlock::typeId, "the default patch starts with the amp");
                session.setBlockAmp (0, amp.id);
                expect (session.getBlockAmp (0) != nullptr && session.getBlockAmp (0)->id == amp.id);

                auto* live = engine.getChain()->getBlocks()[0].get();
                const auto& inputRange = live->findParameter ("input")->getSpec().range;
                expectWithinAbsoluteError (live->findParameter ("input")->getValue(), inputRange.convertFrom0to1 (0.6f), 0.01f, "Input at the face's 6");
                const auto& outputRange = live->findParameter ("output")->getSpec().range;
                expectWithinAbsoluteError (live->findParameter ("output")->getValue(), outputRange.convertFrom0to1 (0.65f), 0.01f);
                expectWithinAbsoluteError (live->findParameter ("normalise")->getValue(), 1.0f, 0.001f, "Level match on (the push button out)");
                expectWithinAbsoluteError (session.getPatch (key)->mainLane().blocks[0].params.at ("input"), inputRange.convertFrom0to1 (0.6f), 0.01f,
                                           "saved with the patch");

                // A knob turned afterwards is the patch's own: the amp keeps its starting settings.
                live->findParameter ("input")->setValue (3.0f);
                session.markEdited();
                expect (session.save().wasOk());
                expectWithinAbsoluteError (session.getAmp (amp.id)->findPart ("k1")->value, 0.6f, 0.0001f);

                // Wrong blocks are ignored.
                session.setBlockAmp (1, amp.id);
                expect (session.getBlockAmp (1) == nullptr, "a cab has no face");
            }

            {
                ShowSession session (engine);
                expect (session.openShow (temp.getChildFile ("Amps.encore")).wasOk());
                pumpUntil (engine, [&] { return ! session.isBuilding(); });
                expect (session.getBlockAmp (0) != nullptr, "reopened: the block still shows its amp");
                expectWithinAbsoluteError (session.getPatch (session.getActivePatchId())->mainLane().blocks[0].params.at ("input"), 3.0f, 0.01f,
                                           "the patch's own value");
            }

            engine.audioDeviceStopped();
            temp.deleteRecursively();
        }

        beginTest ("v3.2 targets: the inspector's swatches, the faces and the knob sheets (docs/design/reference/v32-README.md)");
        {
            ScopedEncoreLook look;

            // The knob sheets: every style (rows) in every size (columns) on a panel strip, value 0.5;
            // and the three scales around a chickenhead. Built as the target page builds them.
            const auto knobSheet = [this] (const juce::String& panel, const juce::StringArray& styles, const juce::StringArray& scales,
                                           const juce::String& name)
            {
                const auto rows = juce::jmax (styles.size(), scales.size());
                for (const auto scale : { 1.0f, 2.0f })
                {
                    juce::Image image (juce::Image::ARGB, juce::roundToInt (588 * scale), juce::roundToInt ((16 + 154 * rows) * scale), true);
                    juce::Graphics g (image);
                    g.addTransform (juce::AffineTransform::scale (scale));
                    g.fillAll (juce::Colour (theme::colour::window));
                    AmpPreset preset;
                    preset.look.panel = panel;

                    for (int row = 0; row < rows; ++row)
                    {
                        const auto stripY = 16 + 154 * row;
                        g.drawImage (kit::ampArt::get ("panel_tall_" + panel + ".webp"), juce::Rectangle<float> (16.0f, (float) stripY, 556.0f, 138.0f));
                        const juce::StringArray letters { "S", "M", "L", "XL" };

                        for (int col = 0; col < 4; ++col)
                        {
                            AmpPart part;
                            part.id = "k";
                            part.type = "knob";
                            part.style = styles.isEmpty() ? juce::String ("chicken") : styles[row];
                            part.size = letters[col];
                            part.scale = scales.isEmpty() ? juce::String ("none") : scales[row];
                            part.drives = "input";
                            part.value = 0.5f;
                            const auto size = part.width();
                            part.x = 16 + 86 + 128 * col - size / 2;
                            part.y = stripY + 69 - size / 2;
                            kit::AmpFace::paintPrint (g, preset, part);
                            kit::AmpFace::paintBody (g, preset, part, 0.5f, false);
                        }
                    }

                    writeSnapshot (image, name + (scale > 1.0f ? "@2x" : ""));
                    expect (image.isValid());
                }
            };

            const juce::StringArray styles { "chicken", "black", "plain", "cream", "silver" };
            knobSheet ("gold", styles, {}, "v32-knobs-gold");
            knobSheet ("black", styles, {}, "v32-knobs-black");
            knobSheet ("gold", {}, { "ticks", "n10", "n12" }, "v32-knob-scales-gold");
            knobSheet ("black", {}, { "ticks", "n10", "n12" }, "v32-knob-scales-black");

            // The faces, 600 x 430, transparent around the amp.
            const auto example = [] (const juce::String& id) -> const AmpPreset*
            {
                for (const auto& e : kit::ampTemplates::examples())
                    if (e.amp.id == id)
                        return &e.amp;
                return nullptr;
            };
            const std::pair<const char*, const AmpPreset*> faces[] { { "v32-face-gold", &kit::ampTemplates::defaultFace() },
                                                                       { "v32-face-jc120", example ("example-jc-120") },
                                                                       { "v32-face-5150", example ("example-5150-iii-50w") } };
            for (const auto& [name, amp] : faces)
            {
                expect (amp != nullptr, name);

                if (amp != nullptr)
                {
                    writeSnapshot (kit::AmpFace::compose (*amp, kit::AmpFace::Layer::everything, 1.0f), name);
                    writeSnapshot (kit::AmpFace::compose (*amp, kit::AmpFace::Layer::everything, 2.0f), juce::String (name) + "@2x");
                }
            }

            // The inspector column (from its header), as the board's 1440 x 900 page shows it.
            FakeDevice device;
            AudioEngine engine;
            engine.audioDeviceAboutToStart (&device);
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("encore-targets-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            temp.createDirectory();

            {
                ShowSession session (engine);
                expect (session.newShow (temp.getChildFile ("Targets.encore"), "Targets").wasOk());
                pumpUntil (engine, [&] { return ! session.isBuilding(); });
                AmpLibrary library (temp.getChildFile ("Amps"));
                AmpBuilderView builder (session, library);
                builder.setBounds (0, 0, theme::size::referenceWidth, theme::size::referenceHeight - 44);

                const auto inspectorShot = [&] (const juce::String& name, int height)
                {
                    const auto* inspector = builder.getInspector();
                    expect (inspector != nullptr);
                    const auto top = inspector->getY() - theme::amp::headerH;   // the column header, then the inspector
                    const auto area = juce::Rectangle<int> (inspector->getX() - 1, top, 300, height);
                    for (const auto scale : { 1.0f, 2.0f })
                        writeSnapshot (builder.createComponentSnapshot (area, false, scale), name + (scale > 1.0f ? "@2x" : ""));
                };

                builder.edit (kit::ampTemplates::defaultFace());
                builder.select ({});
                inspectorShot ("v32-amp-inspector-gold", 650);

                {
                    // Painting never changes the layout: the toolbar looks the same after many repaints
                    // (the title area lost 15 px on every paint, leaving a stray mark by the amp picker).
                    const auto toolbar = juce::Rectangle<int> (0, 0, 520, theme::size::outerPadding + theme::size::subToolbar);
                    const auto first = builder.createComponentSnapshot (toolbar, false, 1.0f);

                    for (int i = 0; i < 12; ++i)
                        builder.createComponentSnapshot (toolbar, false, 1.0f);

                    const auto later = builder.createComponentSnapshot (toolbar, false, 1.0f);
                    auto same = first.getBounds() == later.getBounds();

                    for (int y = 0; same && y < first.getHeight(); ++y)
                        for (int x = 0; same && x < first.getWidth(); ++x)
                            same = first.getPixelAt (x, y) == later.getPixelAt (x, y);

                    expect (same, "the builder's toolbar is the same after 13 repaints");
                }

                expect (builder.showSwatchFocus ("Tolex", 4), "keyboard focus on the Tolex swatch Cream");
                inspectorShot ("v32-amp-inspector-focus", 650);
                builder.showSwatchFocus ({}, -1);

                if (const auto* amp = example ("example-5150-iii-50w"))
                {
                    builder.edit (*amp);
                    builder.select ({});
                    inspectorShot ("v32-amp-inspector-grille-head", 650);
                }

                builder.edit (kit::ampTemplates::defaultFace());
                builder.select ("k4");
                inspectorShot ("v32-amp-inspector-knob", 598);

                if (const auto* amp = example ("example-jc-120"))
                {
                    builder.edit (*amp);
                    builder.select ("s1");
                    inspectorShot ("v32-amp-inspector-section", 370);
                }
            }

            engine.audioDeviceStopped();
            temp.deleteRecursively();
        }
    }
};

static AmpBuilderTests ampBuilderTests;

} // namespace encore::test
