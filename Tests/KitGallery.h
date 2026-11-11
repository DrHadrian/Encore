// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/kit/DialogFrame.h"
#include "ui/kit/GearPlaceholder.h"
#include "ui/kit/IconButton.h"
#include "ui/kit/KeyCap.h"
#include "ui/kit/KnobView.h"
#include "ui/kit/Lcd.h"
#include "ui/kit/MeterBar.h"
#include "ui/kit/NoticeBar.h"
#include "ui/kit/Pane.h"
#include "ui/kit/SegmentedControl.h"
#include "ui/kit/StatusBadge.h"
#include "ui/kit/Toolbar.h"
#include "ui/theme/EncoreTheme.h"
#include "ui/theme/Icons.h"
#include "ui/theme/Paint.h"

namespace encore::test
{

inline juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

/** Every kit component in its states on one page (kit.png), for review.
    Test-only: the app never builds it. Positions here are the gallery's own
    layout, not design values. */
class KitGallery final : public juce::Component
{
public:
    KitGallery()
    {
        using namespace theme;
        setOpaque (true);

        addAndMakeVisible (toolbar);
        toolbar.addAndMakeVisible (mode);
        mode.setSegments ({ "Stage", "Edit", "Layout" });
        mode.setSelected (1);
        toolbar.addAndMakeVisible (lcd);
        lcd.setItems ({ { "1 / 8", "Song", 0 }, { "Clean", "Patch Clean", size::lcdMinPatch }, { "20.0 ms", "Round trip", 0 }, { "48.0k", "Rate", 0 } });

        const std::pair<const char*, Variant> buttonKinds[] = {
            { "Tuner", Variant::standard }, { "Done", Variant::primary }, { "PANIC", Variant::danger },
            { "MUTED", Variant::muted }, { "Full list", Variant::small }, { "Disabled", Variant::standard },
        };

        for (const auto& [text, variant] : buttonKinds)
        {
            auto* b = buttons.add (new juce::TextButton (text));
            setVariant (*b, variant);
            addAndMakeVisible (b);
        }

        buttons.getLast()->setEnabled (false);
        auto* toggled = buttons.add (new juce::TextButton ("Inspector"));
        toggled->setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (toggled);

        for (auto icon : { Icon::plus, Icon::minus, Icon::chevronUp, Icon::chevronDown, Icon::duplicate, Icon::trash, Icon::sidebarRight })
            addAndMakeVisible (iconButtons.add (new kit::IconButton (icon, "Icon")));

        iconButtons.getLast()->setToggleState (true, juce::dontSendNotification);

        notice.setText ("Encore restarted after an unexpected exit - back at cue 1 Clean.", utf8 ("shows for 30 s \xc2\xb7 written to the event log"));
        stageNotice.setText ("Go to cue 8_   (Enter to go, Esc to cancel)");
        lost.setText ("AUDIO INTERFACE LOST - reconnecting to Focusrite USB ASIO...",
                      juce::String (juce::CharPointer_UTF8 ("Staying on cue 3 \xc2\xb7 retrying every second \xc2\xb7 check the cable")));
        muted.setText (juce::String (juce::CharPointer_UTF8 ("Output muted \xe2\x80\x94 click to go live")));

        for (auto* n : { &notice, &stageNotice, &lost, &muted })
            addAndMakeVisible (n);

        pane.setHeader ("Set list", "6.9% CPU");
        addAndMakeVisible (pane);
        plainPane.setHeader (utf8 ("Inspector \xc2\xb7 Amp"));
        addAndMakeVisible (plainPane);

        addAndMakeVisible (stageLcd);
        stageLcd.setItems ({ { "3", "Song", 0 }, { "Ballad in D", "Set 1", size::lcdMinPatchStage } });

        input.setPeak (juce::Decibels::decibelsToGain (-9.4f));
        output.setMuted (true);
        addAndMakeVisible (input);
        addAndMakeVisible (output);

        summary[0].setStatus (kit::Status::problem);
        summary[1].setStatus (kit::Status::warning);
        summary[2].setStatus (kit::Status::ok);

        for (auto& s : summary)
            addAndMakeVisible (s);

        addAndMakeVisible (keyCap);

        const std::tuple<const char*, double, const char*> knobs[] = {
            { "Input", 0.0, "" }, { "Bass", 0.5, "" }, { "Mid", 1.0, "" }, { "Output", 0.62, "EXP 1" }, { "Treble", 0.3, "" },
        };

        for (const auto& [name, value, chip] : knobs)
        {
            auto* k = knobViews.add (new kit::KnobView());
            k->setRange (0.0, 1.0);
            k->setValue (value, juce::dontSendNotification);
            k->setLabel (name);
            k->setChip (chip);
            addAndMakeVisible (k);
        }

        knobViews.getLast()->setEnabled (false);

        toggleOn.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (toggleOn);
        addAndMakeVisible (toggleOff);
        addAndMakeVisible (ledOn);
        ledOff.setOn (false);
        addAndMakeVisible (ledOff);

        using Shape = kit::BlockInfo::Shape;
        using Size = kit::GearView::Size;
        const auto amp = kit::BlockInfo { Shape::amp, "Amp", "JC 120B", "JC 120B", true };
        const auto cab = kit::BlockInfo { Shape::cab, "Cab", "2 x 12", {}, true };

        gear.add (new kit::GearPlaceholder (Size::chainTile, amp));
        gear.add (new kit::GearPlaceholder (Size::chainTile, cab));
        gear.add (new kit::GearPlaceholder (Size::chainTile, { Shape::pedal, "Gate", "Noise gate", {}, false }));
        gear.add (new kit::GearPlaceholder (Size::chainTile, { Shape::pedal, "Delay", "Delay", {}, true }));
        gear.add (new kit::GearPlaceholder (Size::stagePedal, { Shape::pedal, "Drive", "Drive", {}, true }));
        gear.add (new kit::GearPlaceholder (Size::panel, { Shape::amp, juce::String (juce::CharPointer_UTF8 ("Amp \xc2\xb7 NAM capture")), "JC 120B", "JC 120B", true }));
        gear.add (new kit::GearPlaceholder (Size::panel, { Shape::cab, juce::String (juce::CharPointer_UTF8 ("Cab \xc2\xb7 IR")), "2 x 12", {}, true }));

        for (auto* g : gear)
            addAndMakeVisible (g);

        checkOn.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (checkOn);
        addAndMakeVisible (checkOff);
        combo.addItemList ({ "1: Input channel 1", "2: Input channel 2" }, 1);
        combo.setSelectedId (1);
        setVariant (combo, Variant::field);
        addAndMakeVisible (combo);
        popup.addItemList ({ "48 000 Hz", "44 100 Hz" }, 1);
        popup.setSelectedId (1);
        addAndMakeVisible (popup);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setValue (0.6);
        addAndMakeVisible (slider);
        editor.setText ("Ballad in D");
        addAndMakeVisible (editor);

        frame.setTabs ({ { "Audio", Icon::audioWave }, { "MIDI", Icon::midiPort } });
        frame.setFooterText ("Changes apply immediately.");
        frame.addFooterButton ("Cancel");
        frame.addFooterButton ("Done", Variant::primary);
        addAndMakeVisible (overlay);

        setSize (1440, 1320);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (paint::c (colour::window));

        // The chain board behind the tiles, with the pedals' shadows under them.
        g.setColour (paint::c (colour::chainBoard));
        g.fillRect (board);

        for (auto* p : gear)
            if (p->getBlock().shape == kit::BlockInfo::Shape::pedal)
                kit::GearPlaceholder::paintPedalShadow (g, p->getBounds().toFloat());

        kit::MeterBar::paintScale (g, input.getTrackBounds().translated ((float) input.getX(), 0.0f),
                                   output.getBounds().toFloat().translated (0.0f, (float) output.getHeight() + 2.0f).withHeight ((float) size::meterScaleH));

        auto badges = juce::Rectangle<float> (820.0f, 520.0f, (float) size::statusBadge, (float) size::statusBadge);

        for (auto s : { kit::Status::problem, kit::Status::warning, kit::Status::ok })
        {
            kit::paintStatusBadge (g, badges, s);
            badges.translate ((float) size::statusBadge + 10.0f, 0.0f);
        }

        kit::paintSubheader (g, { 20, 640, 280, 24 }, "PARAMETERS", juce::String (juce::CharPointer_UTF8 ("value \xc2\xb7 pedal")));
        kit::paintGroupLabel (g, { 20.0f, 690.0f, 74.0f, 12.0f }, "PREAMP", kit::groupLabelWidth ("PREAMP"), false);
        kit::paintGroupLabel (g, { 104.0f, 690.0f, 276.0f, 12.0f }, "EQ", kit::groupLabelWidth ("EQ"), true);
    }

    void resized() override
    {
        using namespace theme;
        toolbar.setBounds (0, 0, getWidth(), size::toolbarEdit + 1);
        toolbar.setLogoBounds ({ 14, 12, 98, 17 });
        mode.setBounds (287, 8, 194, 28);
        lcd.setBounds (529, 5, lcd.getIdealWidth(), kit::Lcd::heightFor (false));

        auto x = 20;

        for (auto* b : buttons)
        {
            const auto h = getVariant (*b) == Variant::small ? size::buttonSmallHeight : size::buttonHeight;
            const auto w = b->getBestWidthForHeight (h);
            b->setBounds (x, 60, w, h);
            x += w + 10;
        }

        for (auto* b : iconButtons)
        {
            b->setBounds (x, 62, size::iconButton, size::iconButton);
            x += size::iconButton + 4;
        }

        notice.setBounds (0, 100, getWidth(), size::noticeBar + 1);
        stageNotice.setBounds (0, 140, getWidth(), size::stageBanner + 1);
        lost.setBounds (0, 184, getWidth(), size::stageBanner + 1);
        muted.setBounds (0, 228, getWidth(), size::mutedBanner);

        pane.setBounds (20, 280, 250, 200);
        plainPane.setBounds (290, 280, 272, 200);
        stageLcd.setBounds (600, 290, stageLcd.getIdealWidth(), kit::Lcd::heightFor (true));
        input.setBounds (600, 350, 760, 14);
        output.setBounds (600, 372, 760, 14);

        x = 600;

        for (auto& s : summary)
        {
            s.setBounds (x, 420, kit::SummaryBadge::getIdealSize(), kit::SummaryBadge::getIdealSize());
            x += kit::SummaryBadge::getIdealSize() + 10;
        }

        keyCap.setBounds (1000, 520, keyCap.getIdealWidth(), 14);

        x = 20;

        for (auto* k : knobViews)
        {
            k->setBounds (kit::KnobView::idealBounds().withPosition (x, 720));
            x += size::knobColumn;
        }

        toggleOn.setBounds (kit::GearToggle::idealBounds().withPosition (x, 720));
        x += size::toggleColumn;
        toggleOff.setBounds (kit::GearToggle::idealBounds().withPosition (x, 720));
        x += size::toggleColumn;
        ledOn.setBounds (x, 720, size::ledHalo, size::ledHalo);
        ledOff.setBounds (x + size::ledHalo + 10, 720, size::ledHalo, size::ledHalo);

        board = { 20, 840, 560, 112 };
        x = 32;

        for (int i = 0; i < 4; ++i)
        {
            const auto r = kit::GearView::idealBounds (kit::GearView::Size::chainTile, gear[i]->getBlock().shape);
            gear[i]->setBounds (r.withPosition (x, board.getY() + 14));
            x += r.getWidth() + size::tileGap;
        }

        gear[4]->setBounds (380, 990, size::pedalW * 7 / 10, size::pedalH * 7 / 10);   // the Stage scale (0.7)
        gear[5]->setBounds (kit::GearView::idealBounds (kit::GearView::Size::panel, kit::BlockInfo::Shape::amp).withPosition (600, 840));
        gear[6]->setBounds (kit::GearView::idealBounds (kit::GearView::Size::panel, kit::BlockInfo::Shape::cab).withPosition (1200, 840));

        checkOn.setBounds (20, 1000, 140, 20);
        checkOff.setBounds (20, 1024, 140, 20);
        combo.setBounds (20, 1056, 228, size::fieldHeight);
        popup.setBounds (20, 1088, 140, size::buttonHeight);
        slider.setBounds (20, 1124, 140, 20);
        editor.setBounds (180, 1000, 160, size::fieldHeight);

        overlay.setBounds (20, 1160, 560, 150);
    }

private:
    kit::Toolbar toolbar;
    kit::SegmentedControl mode;
    kit::Lcd lcd { false }, stageLcd { true };
    juce::OwnedArray<juce::TextButton> buttons;
    juce::OwnedArray<kit::IconButton> iconButtons;
    kit::NoticeBar notice { kit::NoticeBar::Kind::notice }, stageNotice { kit::NoticeBar::Kind::stageNotice },
                   lost { kit::NoticeBar::Kind::lost }, muted { kit::NoticeBar::Kind::muted };
    kit::Pane pane, plainPane;
    kit::MeterBar input { "Input" }, output { "Output" };
    kit::SummaryBadge summary[3];
    kit::KeyCap keyCap { "F6" };
    juce::OwnedArray<kit::KnobView> knobViews;
    kit::GearToggle toggleOn { "Level match" }, toggleOff { "Level match" };
    kit::PowerLed ledOn, ledOff;
    juce::OwnedArray<kit::GearPlaceholder> gear;
    juce::Rectangle<int> board;
    juce::ToggleButton checkOn { "Level match" }, checkOff { "Detect guitar" };
    juce::ComboBox combo, popup;
    juce::Slider slider;
    juce::TextEditor editor;
    kit::DialogFrame frame { utf8 ("Settings \xe2\x80\x94 Audio"), "Close settings" };
    kit::DialogOverlay overlay { frame, { 520, 230 } };
};

} // namespace encore::test
