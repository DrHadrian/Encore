// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// Design tokens of the Encore UI (design v3), as C++ constants.
// Use it everywhere: no
// colour, size or font literal may appear in UI code outside this file.
// Source of truth: docs/design/DESIGN-SYSTEM.md (values here match it 1:1).
//
// All sizes are logical pixels at 100 % scale (JUCE's coordinate units), taken
// from the 1440 x 900 reference boards in docs/design/reference/.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace encore::theme
{

//==============================================================================
// Colours (0xAARRGGBB)
namespace colour
{
    // Window and surfaces
    constexpr juce::uint32 window          = 0xff161618;   // app background
    constexpr juce::uint32 pane            = 0xff1D1D20;   // every panel
    constexpr juce::uint32 paneBorder      = 0xff0A0A0B;   // 1 px around panels, under toolbars
    constexpr juce::uint32 paneHighlight   = 0x0AFFFFFF;   // 1 px top inner highlight of panes (white 4 %)
    constexpr juce::uint32 paneHeaderTop   = 0xff252528;   // pane header gradient, top
    constexpr juce::uint32 paneHeaderBot   = 0xff202023;   // pane header gradient, bottom
    constexpr juce::uint32 paneHeaderLine  = 0xff111113;   // line under a pane header / between sections
    constexpr juce::uint32 listBackground  = 0xff18181B;   // inside the full list table
    constexpr juce::uint32 rowAlternate    = 0x06FFFFFF;   // every other table row (white 2.5 %)
    constexpr juce::uint32 rowLine         = 0x66000000;   // 1 px between table rows (black 40 %)

    // Toolbar (top bar)
    constexpr juce::uint32 toolbarTop      = 0xff333337;
    constexpr juce::uint32 toolbarBot      = 0xff28282B;
    constexpr juce::uint32 toolbarHighlight= 0x12FFFFFF;   // 1 px top inner highlight (white 7 %)
    constexpr juce::uint32 subToolbarTop   = 0xff2A2A2E;   // full-list toolbar
    constexpr juce::uint32 subToolbarBot   = 0xff232326;

    // Text
    constexpr juce::uint32 text            = 0xffE8E8EB;   // primary
    constexpr juce::uint32 textStrong      = 0xffFFFFFF;   // on blue, titles of section rows
    constexpr juce::uint32 textSecondary   = 0xffA9A9AF;   // labels, help
    constexpr juce::uint32 textTertiary    = 0xff8E8E95;   // field labels ("k"), summaries
    constexpr juce::uint32 textMuted       = 0xff85858C;   // pane header labels, footers
    constexpr juce::uint32 textDisabled    = 0xff6E6E75;
    constexpr juce::uint32 textDim         = 0xff7A7A80;   // played rows, numbers
    constexpr juce::uint32 textSoft        = 0xffC4C4C9;   // secondary values, title bar text
    constexpr juce::uint32 textRow         = 0xffE1E1E5;   // list rows
    constexpr juce::uint32 logoUi          = 0xffD9D9DE;   // wordmark in the toolbar
    constexpr juce::uint32 logoCream       = 0xffF3E6C4;   // wordmark on gear (not used while gear is placeholder)

    // Buttons
    constexpr juce::uint32 buttonTop       = 0xff4A4A4F;
    constexpr juce::uint32 buttonBot       = 0xff3A3A3E;
    constexpr juce::uint32 buttonBorder    = 0xff0E0E10;
    constexpr juce::uint32 buttonHighlight = 0x1FFFFFFF;   // 1 px top inner highlight (white 12 %)
    constexpr juce::uint32 buttonDrop      = 0x66000000;   // 1 px drop under buttons (black 40 %)
    constexpr juce::uint32 pressedTop      = 0xff252528;   // pressed / toggled-on ("b-on")
    constexpr juce::uint32 pressedBot      = 0xff2E2E31;
    constexpr juce::uint32 pressedShadow   = 0xB3000000;   // inner shadow of pressed buttons (black 70 %)
    constexpr juce::uint32 iconButtonHover = 0xff2E2E32;

    // Selection / primary (blue)
    constexpr juce::uint32 accent          = 0xff4C8DF6;   // focus ring, selection outline, links
    constexpr juce::uint32 accentHover     = 0xff82AEFA;
    constexpr juce::uint32 primaryTop      = 0xff4C8DF6;   // primary button gradient
    constexpr juce::uint32 primaryBot      = 0xff2E6FDC;
    constexpr juce::uint32 primaryBorder   = 0xff0B3E86;
    constexpr juce::uint32 selectedRow     = 0xff2E62C4;   // selected / playing row in panes and stage lists
    constexpr juce::uint32 selectedRowSub  = 0xffD3E1FA;   // secondary text on a selected row
    constexpr juce::uint32 tableSelection  = 0x594C8DF6;   // accent at 35 %: selected row in the full list
    constexpr juce::uint32 menuHighlight   = 0xff2E62C4;

    // Status
    constexpr juce::uint32 healthy         = 0xff30C25A;   // playing bar, OK, live dots
    constexpr juce::uint32 okBadgeFill     = 0xff1E3A24;
    constexpr juce::uint32 okBadgeTick     = 0xff4CD964;
    constexpr juce::uint32 amber           = 0xffFFB340;   // warnings, section headings (at 85 %), bar marks
    constexpr juce::uint32 amberSection    = 0xD9FFB340;   // amber at 85 %: section headings / kinds
    constexpr juce::uint32 amberStrong     = 0xffFF9F0A;   // amber badge fill
    constexpr juce::uint32 amberInk        = 0xff1A1200;   // text on amber badge
    constexpr juce::uint32 noticeTop       = 0xff3D2F12;   // notice bar gradient
    constexpr juce::uint32 noticeBot       = 0xff30250E;
    constexpr juce::uint32 noticeText      = 0xffFFD27A;
    constexpr juce::uint32 noticeSubtle    = 0xffC9A964;
    constexpr juce::uint32 danger          = 0xffFF8A80;   // danger text (PANIC label, Remove, Delete)
    constexpr juce::uint32 mutedTop        = 0xffE8483E;   // MUTED button
    constexpr juce::uint32 mutedBot        = 0xffBF2C25;
    constexpr juce::uint32 mutedBorder     = 0xff6E120D;
    constexpr juce::uint32 mutedBanner     = 0xffC9302A;   // "Output muted — click to go live"
    constexpr juce::uint32 panicTop        = 0xffA62B24;   // panic tile, AUDIO LOST widget
    constexpr juce::uint32 panicBot        = 0xff761A15;
    constexpr juce::uint32 panicBorder     = 0xffC2392F;
    constexpr juce::uint32 lostTop         = 0xff5A1A16;   // interface-lost banner
    constexpr juce::uint32 lostBot         = 0xff431311;
    constexpr juce::uint32 lostText        = 0xffFFB4AD;
    constexpr juce::uint32 lostSubText     = 0xffE8A8A2;
    constexpr juce::uint32 lostWidgetSub   = 0xffFFD3CE;
    constexpr juce::uint32 problemTint     = 0x14D83A30;   // pre-show problem row tint (8 %)
    constexpr juce::uint32 problemBadge    = 0xffD83A30;
    constexpr juce::uint32 problemBadgeHi  = 0xffFF7A70;   // summary badge: radial gradient light spot
    constexpr juce::uint32 problemBadgeLo  = 0xff8E1E18;   // summary badge: radial gradient rim
    constexpr juce::uint32 problemGlow     = 0x73D83A30;   // summary badge glow (45 %)
    constexpr juce::uint32 problemText     = 0xffFF8A80;
    constexpr juce::uint32 problemBandTop  = 0xff2A1E1E;   // pre-show summary band when red
    constexpr juce::uint32 problemBandBot  = 0xff241C1D;
    constexpr juce::uint32 warningBandTop  = 0xff2A251C;   // ... amber and green at the same darkness
    constexpr juce::uint32 warningBandBot  = 0xff24211C;
    constexpr juce::uint32 okBandTop       = 0xff1D2A21;
    constexpr juce::uint32 okBandBot       = 0xff1C241E;
    constexpr juce::uint32 problemRowTint  = 0x14D83A30;   // a problem line in the pre-show check (8 %)
    constexpr juce::uint32 checkRowLine    = 0xff1A1A1C;   // between pre-show check lines

    // Fields
    constexpr juce::uint32 fieldFill       = 0xff141416;
    constexpr juce::uint32 fieldBorder     = 0xff0C0C0E;
    constexpr juce::uint32 cellEditorFill  = 0xff0E1013;   // in-place editor in the full list
    constexpr juce::uint32 kbdBorder       = 0xff2E2E33;   // key caps ("F6")

    // LCD
    constexpr juce::uint32 lcdTop          = 0xff1C232C;
    constexpr juce::uint32 lcdBot          = 0xff12171D;
    constexpr juce::uint32 lcdBorder       = 0xff07090B;
    constexpr juce::uint32 lcdText         = 0xffD5E4F7;
    constexpr juce::uint32 lcdLabel        = 0xff7F95B3;
    constexpr juce::uint32 lcdShadow       = 0xA6000000;   // inner shadow (black 65 %)
    constexpr juce::uint32 lcdHighlight    = 0x0DFFFFFF;   // 1 px bottom highlight (white 5 %)

    // Stage widgets
    constexpr juce::uint32 widget          = 0xff1C1E23;
    constexpr juce::uint32 widgetBorder    = 0xff2B2E35;
    constexpr juce::uint32 widgetTagFill   = 0x8C000000;   // name tag (black 55 %)
    constexpr juce::uint32 widgetTagText   = 0xffD8D8DC;
    constexpr juce::uint32 widgetTitle     = 0xff6E7480;   // "SONG 3   SET 1"
    constexpr juce::uint32 widgetTrack     = 0xff2A2D33;   // empty meter / pedal track
    constexpr juce::uint32 expressionTop   = 0xff6EA5FA;   // EXP bar gradient, top (bottom: expressionBot)
    constexpr juce::uint32 stageMeterLow   = 0xff2BC24F;   // stage level bars: green to half way...
    constexpr juce::uint32 stageMeterHigh  = 0xffE8CF3A;   // ...then towards yellow
    constexpr juce::uint32 stageMeterClip  = 0xffE5484D;   // a stage level bar while the input clips
    constexpr juce::uint32 stageBoardShadow= 0xBF000000;   // inset shadow of the stomp board (black 75 %)
    constexpr juce::uint32 tunerTick       = 0xff25303C;   // tuner scale ticks
    constexpr juce::uint32 tunerTickCentre = 0xff55657A;
    constexpr juce::uint32 tunerDetail     = 0xffA5B8D2;   // "−2 cents · 109.8 Hz · A = 440 Hz"

    // Meters
    constexpr juce::uint32 meterTrack      = 0xff0E0E10;
    constexpr juce::uint32 meterGreen      = 0xff2BC24F;
    constexpr juce::uint32 meterYellow     = 0xffE8CF3A;
    constexpr juce::uint32 meterPeak       = 0xffE8E8EB;
    constexpr juce::uint32 meterScale      = 0xff6A6A71;
    constexpr juce::uint32 expressionFill  = 0xff6EA5FA;   // EXP bars, assigned parameter bars
    constexpr juce::uint32 parameterFill   = 0xff8E8E95;
    constexpr juce::uint32 expChipFill     = 0xff1B2C45;   // "EXP 1" chip
    constexpr juce::uint32 expChipText     = 0xff7FC4FF;
    constexpr juce::uint32 pedalBar        = 0xff6EA5FA;   // inspector value bar of a parameter a pedal moves

    // Menus, dialogs, scrollbars
    constexpr juce::uint32 menu            = 0xF72C2C30;   // rgba(44,44,48,.97)
    constexpr juce::uint32 menuHighlightBorder = 0x14FFFFFF;
    constexpr juce::uint32 menuSeparator   = 0xff3A3A3F;
    constexpr juce::uint32 menuShortcut    = 0xff85858C;
    constexpr juce::uint32 dialog          = 0xff232326;
    constexpr juce::uint32 dialogTitleTop  = 0xff38383C;
    constexpr juce::uint32 dialogTitleBot  = 0xff2E2E31;
    constexpr juce::uint32 dialogFooter    = 0xff262629;
    constexpr juce::uint32 dimmer          = 0x73000000;   // behind modal dialogs (black 45 %)
    constexpr juce::uint32 dialogHighlight = 0x14FFFFFF;   // inner top highlight (white 8 %)
    constexpr juce::uint32 groupHighlight  = 0x08FFFFFF;   // a group box's inner top line (white 3 %)
    constexpr juce::uint32 dialogShadow    = 0xB3000000;   // 0, 30, 80 px, black 70 %
    constexpr juce::uint32 tabStripTop     = 0xff2E2E31;   // settings tab strip
    constexpr juce::uint32 tabStripBot     = 0xff28282B;
    constexpr juce::uint32 scrollThumb     = 0xff4A4A4F;
    constexpr juce::uint32 scrollThumbHover= 0xff5E5E64;

    // Gear placeholders (until real gear art exists)
    constexpr juce::uint32 gearTop         = 0xff2C2C30;
    constexpr juce::uint32 gearBot         = 0xff1F1F22;
    constexpr juce::uint32 gearBorder      = 0xff3A3A3F;
    constexpr juce::uint32 gearStrip       = 0xff26262A;
    constexpr juce::uint32 gearStripBorder = 0xff38383D;
    constexpr juce::uint32 gearLabel       = 0xff85858C;
    constexpr juce::uint32 gearHighlight   = 0x14FFFFFF;   // inner top highlight (white 8 %)
    constexpr juce::uint32 pedalTop        = 0xff303034;   // pedal placeholder gradient
    constexpr juce::uint32 pedalBot        = 0xff232326;
    constexpr juce::uint32 pedalShadow     = 0x73000000;   // pedal drop shadow (0, 6, 10 px, black 45 %)
    constexpr juce::uint32 pedalName       = 0xffE8E8EB;
    constexpr juce::uint32 footswitchTop   = 0xff4A4A4F;
    constexpr juce::uint32 footswitchBot   = 0xff2E2E32;
    constexpr juce::uint32 footswitchBorder= 0xff0E0E10;
    constexpr juce::uint32 ledGlow         = 0xCCFF3B30;   // LED glow (0 0 8 px, 80 %)
    constexpr juce::uint32 ledHaloCore     = 0x8CFF6E3C;   // power LED halo: 55 % at the centre
    constexpr juce::uint32 ledHaloMid      = 0x38FF5028;   //   22 % at 35 %
    constexpr juce::uint32 ledHaloEdge     = 0x00FF3C1E;   //   0 at 70 %
    constexpr juce::uint32 groupRule       = 0xff3A3A3F;   // rules beside PREAMP / EQ / OUTPUT
    constexpr juce::uint32 toggleBorder    = 0xff4A4A4F;
    constexpr juce::uint32 knobLabelText   = 0xffC4C4C9;   // INPUT, BASS... under knobs
    constexpr juce::uint32 knobTop         = 0xff4A4A4F;
    constexpr juce::uint32 knobBot         = 0xff2A2A2E;
    constexpr juce::uint32 knobPointer     = 0xffE8E8EB;
    constexpr juce::uint32 tickMark        = 0xff6E6E75;
    constexpr juce::uint32 knobShadow      = 0x80000000;   // knob drop shadow (0, 2, 3 px, black 50 %)
    constexpr juce::uint32 knobChipFill    = 0xff141414;   // "EXP 1" chip under a gear knob
    constexpr juce::uint32 ledOn           = 0xffFF3B30;
    constexpr juce::uint32 ledOff          = 0xff3A1C1A;
    constexpr juce::uint32 chainBoard      = 0xff19191B;   // signal-chain board (was tolex)

    // Added while building the design in JUCE (values read from the reference boards)
    constexpr juce::uint32 fieldText       = 0xffE1E1E5;   // text in inset fields and popup buttons
    constexpr juce::uint32 textSelection   = 0x664C8DF6;   // selected text in fields (accent 40 %)
    constexpr juce::uint32 fieldShadow     = 0x99000000;   // inner shadow of inset fields (black 60 %)
    constexpr juce::uint32 sliderTrack     = 0xff0E0E10;   // Out volume slider track
    constexpr juce::uint32 sliderFill      = 0xff6E6E75;   // Out volume slider, filled part
    constexpr juce::uint32 thumbTop        = 0xffF2F2F4;   // slider thumb / toggle knob gradient
    constexpr juce::uint32 thumbBot        = 0xffB9B9BF;
    constexpr juce::uint32 checkboxBorder  = 0xff4A4A4F;   // unticked checkbox
    constexpr juce::uint32 checkboxFill    = 0xff141416;
    constexpr juce::uint32 rowHover        = 0xff2A2A2F;   // hover on clickable rows
    constexpr juce::uint32 patchSelected   = 0xff2B2B30;   // selected row in the Patches pane
    constexpr juce::uint32 tooltip         = 0xff2C2C30;
    constexpr juce::uint32 groupBox        = 0xff1A1A1D;   // group boxes in dialogs
    constexpr juce::uint32 mappingHeader   = 0xff1E1E21;   // MIDI mappings: column header, bottom bar
    constexpr juce::uint32 mappingRowAlt   = 0xff1C1C1F;   // MIDI mappings: every other row
    constexpr juce::uint32 triggerText     = 0xffD5D5DA;   // "CC 64, any channel"
    constexpr juce::uint32 latencyOut      = 0xff6EA5FA;   // the latency bar's output part (input: accent)
    constexpr juce::uint32 tabOn           = 0xff3A3A3F;   // selected settings tab
    constexpr juce::uint32 dashedTile      = 0xff3E3E42;   // "+" tile border in the chain
    constexpr juce::uint32 plusTile        = 0xffA5A5AB;   // "+" glyph, cab help text
    constexpr juce::uint32 cabCaption      = 0xffE8C27A;   // "Cab · <IR>" caption
    constexpr juce::uint32 stageHeaderTop  = 0xff2A2A2E;   // block stage pane header
    constexpr juce::uint32 stageHeaderBot  = 0xff222225;
    constexpr juce::uint32 dotOff          = 0xff5A5A60;   // header dot while the block is off
    constexpr juce::uint32 tileSelection   = 0xff4FA3FF;   // selected chain tile outline
    constexpr juce::uint32 chainBoardShadow= 0xB3000000;   // inset shadow of the chain board (black 70 %)
    constexpr juce::uint32 stageFooter     = 0xff1B1B1E;   // Stage footer strip
    constexpr juce::uint32 tunerOverlay    = 0xF70E0E10;   // tuner over the grid (97 %)
    constexpr juce::uint32 stageNumberDim  = 0xff7A7A80;   // numbers in stage lists
    constexpr juce::uint32 panicSub        = 0xffF3C4C0;   // "silence & reset all"
    constexpr juce::uint32 expressionBot   = 0xff3A7AE6;   // EXP bar gradient, bottom
    constexpr juce::uint32 blockStageTop   = 0xff3A3A3E;   // block stage radial light, centre
    constexpr juce::uint32 blockStageMid   = 0xff1C1C1F;
    constexpr juce::uint32 blockStageBot   = 0xff141416;
    constexpr juce::uint32 modelPlate      = 0xff3A3A3E;   // amp model plate
    constexpr juce::uint32 gearGroupLabel  = 0xffA9A9AF;   // PREAMP / EQ / OUTPUT
    constexpr juce::uint32 knobBorder      = 0xff0A0A0B;
    constexpr juce::uint32 toggleTrack     = 0xff1A1A1D;
    constexpr juce::uint32 iconChipFill    = 0xff2A2D34;   // layout editor widget icons
    constexpr juce::uint32 iconChipBorder  = 0xff3A3E47;
    constexpr juce::uint32 iconChipText    = 0xffB9BEC9;
    constexpr juce::uint32 gridLine        = 0x0AFFFFFF;   // layout editor cell lines (white 4 %)
    constexpr juce::uint32 handleBorder    = 0xff2E6FDC;   // layout editor resize handles
    constexpr juce::uint32 canvasPane      = 0xff111113;   // layout editor: the pane around the artboard
    constexpr juce::uint32 artboard        = 0xff121418;   // layout editor: the stage preview
    constexpr juce::uint32 artboardBorder  = 0xff000000;
    constexpr juce::uint32 selectionHalo   = 0x404C8DF6;   // accent at 25 %
    constexpr juce::uint32 learningRow     = 0x1FFFB340;   // MIDI row being learned (amber 12 %)

    // Patch colours (PatchModel::colour is used as stored; these are the defaults shown in the designs)
    constexpr juce::uint32 patchBlue       = 0xff4C8DF6;
    constexpr juce::uint32 patchOrange     = 0xffFF9F0A;
    constexpr juce::uint32 patchGreen      = 0xff30C25A;
    constexpr juce::uint32 patchRed        = 0xffFF6B4A;

    // Milestones 10 and 11 (NEW-FEATURES-M10-M11 §C)
    constexpr juce::uint32 chipMuteBg      = 0xff5A1A16;   // OUTPUT MUTED
    constexpr juce::uint32 chipMuteText    = 0xffFFB4AD;
    constexpr juce::uint32 chipAmberBg     = 0xff3D2F12;   // NOT HERE, counts
    constexpr juce::uint32 chipAmberText   = 0xffFFD27A;
    constexpr juce::uint32 chipExpBg       = 0xff1B2C45;   // EXP n, THIS COMPUTER
    constexpr juce::uint32 chipExpText     = 0xff7FC4FF;
    constexpr juce::uint32 chipOffBg       = 0xff2C2C30;   // OFF
    constexpr juce::uint32 chipOffText     = 0xff85858C;
    constexpr juce::uint32 chipGreenBg     = 0xff1E3A24;   // THIS SHOW
    constexpr juce::uint32 chipGreenText   = 0xff4CD964;
    constexpr juce::uint32 chipPlainBg     = 0xff2A2A2E;   // VST3, THIS COMPUTER
    constexpr juce::uint32 chipPlainText   = 0xff8E8E95;
    constexpr juce::uint32 utilityTileTop  = 0xff2C2C30;
    constexpr juce::uint32 utilityTileBottom = 0xff1F1F22;
    constexpr juce::uint32 utilityTileBorder = 0xff3A3A3F;
    constexpr juce::uint32 selectionOutline = 0xff4FA3FF;
    constexpr juce::uint32 faultCardBg     = 0xff2A251C;
    constexpr juce::uint32 faultCardBorder = 0xff3D2F12;
    constexpr juce::uint32 faultCardText   = 0xffFFD27A;
    constexpr juce::uint32 groupRowLine    = 0xff1F1F22;   // 1 px under each row of the Plugins, Folders and Blocked lists
}

//==============================================================================
// Metrics (logical px)
namespace size
{
    // Window
    constexpr int referenceWidth    = 1440, referenceHeight = 900;
    constexpr int minEditWidth      = 1180, minEditHeight = 720;   // below this the edit view scrolls
    constexpr int screenMargin      = 40;    // first window: this much smaller than the screen, at most

    // Toolbars and strips
    constexpr int toolbarEdit       = 44;    // Edit and Layout (+ 1 px bottom line = 45)
    constexpr int toolbarStage      = 48;    // Stage (+ 1 px bottom line = 49)
    constexpr int noticeBar         = 30;    // Edit notice bar
    constexpr int stageBanner       = 34;    // Stage: lost banner, notice strip
    constexpr int mutedBanner       = 28;    // Stage: "Output muted" bar
    constexpr int stageFooter       = 22;
    constexpr int statusBar         = 22;    // layout editor footer
    constexpr int subToolbar        = 40;    // full-list toolbar

    // Layout
    constexpr int outerPadding      = 8;     // around the main grid
    constexpr int gap               = 8;     // between panes, between stage widgets
    constexpr int sidebarWidth      = 250;   // Edit left column
    constexpr int inspectorWidth    = 272;   // Edit right column
    constexpr int widgetPanelWidth  = 220;   // layout editor "+ Widget" panel
    constexpr int layoutInspectorWidth = 280; // layout editor widget inspector
    constexpr int bottomRow         = 104;   // Edit: input / meters / health row

    // Edit toolbar (SCREENS.md §4.1): padding 10, gaps 10
    constexpr int toolbarPadX = 10, toolbarGap = 10, logoMargin = 4, logoW = 98, logoH = 17;
    constexpr int modeSegmentW = 64, modeSwitchGap = 10;    // the switch sits 10 + 10 after the show name
    constexpr int outGap = 6, volumeTrackW = 70, valueFieldW = 66, muteButtonW = 74, outMargin = 4;
    constexpr int buttonDrop = 1;                           // the 1 px drop line under a raised button

    // Edit centre column and bottom row
    constexpr int centreHeaderH = 28, chainPaneH = 139;
    constexpr int inputPadX = 10, inputPadY = 8, inputGap = 6, inputLabelH = 14, deviceLineH = 14;
    constexpr int metersPadX = 12, meterRowH = 14, meterRowGap = 8, meterScaleGap = 7;
    constexpr int healthPadX = 10, healthPadY = 8, healthGap = 3, healthHeaderH = 18, healthLineH = 14;

    // Panes
    constexpr int paneRadius        = 6;
    constexpr int paneHeader        = 24;
    constexpr int paneHeaderPadL    = 10, paneHeaderPadR = 6;

    // Controls
    constexpr int buttonHeight      = 24;
    constexpr int buttonSmallHeight = 18;    // "Full list", "Load amp...", "Reset counters" in panes
    constexpr int buttonRadius      = 5;
    constexpr int buttonPadX        = 10;
    constexpr int toolbarButtonH    = 26;    // segmented mode switch in Edit/Layout
    constexpr int stageToolbarButtonH = 36;  // Stage: Tuner, Mute
    constexpr int stageModeButtonH  = 30;
    constexpr int fieldHeight       = 22;
    constexpr int fieldRadius       = 4;
    constexpr int fieldPadX         = 8, fieldChevron = 9;
    constexpr int patchPickerW      = 130, headerArrow = 14;   // the centre header line
    constexpr int iconButton        = 20;
    constexpr int iconSize          = 12;
    constexpr int rowHeight         = 26;    // pane lists
    constexpr int tableRowHeight    = 28;    // full list
    constexpr int tableHeaderHeight = 24;
    constexpr int sectionHeadingH   = 24;    // sidebar section heading (22 in the CSS; the target renders 24)
    constexpr int headerDot = 7;
    constexpr int inspectorPadX = 10, inspectorPadTop = 6, inspectorPadBottom = 10, inspectorKeyW = 74, inspectorColGap = 8;
    constexpr int inspectorRowGap = 6, inspectorTextH = 15, inspectorListPadX = 6, inspectorNameW = 60, inspectorChipW = 44;
    // Plugins can have hundreds of parameters: the block stage shows the first
    // few as knobs, the inspector lists the first ones; the rest are in the plugin's window.
    constexpr int pluginPanelKnobs = 8, inspectorPluginParams = 48;
    constexpr int inspectorBarH = 4, assignKeyW = 62, assignArrowW = 12;
    constexpr int listPad = 4, listGap = 1, rowPadX = 8, rowGap = 8, rowNumberW = 18, paneBarW = 3, paneBarH = 16, paneBarRadius = 2;
    constexpr int swatch = 9, swatchRadius = 2, rowRadius = 4, headingPadBottom = 2;
    constexpr int paneFooterPadX = 10, paneFooterPadY = 8, headerButtonGap = 1, headerSmallButtonMargin = 4;
    constexpr int menuItemHeight    = 24;
    constexpr int menuPadding       = 5;
    constexpr int menuRadius        = 7;
    constexpr int menuSeparatorH    = 9;     // 1 px line + 4 px above and below
    constexpr int dialogRadius      = 10;
    constexpr int dialogTitleBar    = 30;
    constexpr int dialogFooter      = 48;    // + 1 px line above it
    constexpr int dialogPadX        = 20;    // footer, content
    constexpr int dialogClose       = 20, dialogCloseInset = 8, dialogCloseIcon = 14;
    constexpr int dialogButtonW     = 84, dialogButtonGap = 8;
    constexpr int dialogShadowY     = 30, dialogShadowBlur = 80;
    constexpr int tabStrip          = 78;    // + 1 px line under it
    constexpr int tabW = 78, tabH = 54, tabGap = 4, tabIcon = 22, tabIconGap = 4, tabRadius = 6;
    constexpr int focusRing         = 2;
    constexpr int keyCapRadius      = 3, keyCapPadX = 4;
    constexpr int statusBadge       = 22, statusBadgeLarge = 44, statusGlow = 18;   // pre-show check

    // Gear placeholders (DESIGN-SYSTEM.md §8; real art uses the same boxes, EXTENDING.md §6)
    constexpr int ampTileW = 117, tileW = 64, tileH = 84, plusTileW = 48, tileGap = 6;
    constexpr int pedalW = 160, pedalH = 210;            // drawn at 0.4 in the chain, 0.7 on Stage
    constexpr int ampPanelW = 600, ampPanelH = 430, blockPanelH = 300, cabPanelW = 230, cabPanelH = 300;
    constexpr int ampStripX = 22, ampStripY = 44, ampStripW = 556, ampStripH = 138;
    constexpr int gearRadius = 10, pedalRadius = 14, pedalBorder = 2, stripRadius = 6, plusTileRadius = 8;
    constexpr int chainPadX = 12, chainPadY = 14, chainShadowH = 8, dash = 4;            // the chain board
    constexpr int stagePadTop = 6, stagePadBottom = 14, stagePadX = 12, cabGap = 22;     // the block stage
    constexpr int stripPadTop = 8, stripPadBottom = 6, stripPadX = 18;                   // the amp's control strip
    constexpr int groupLabelH = 12, groupLabelGap = 2, groupLabelPadX = 10, outputGroupW = 170;
    constexpr int powerColumn = 40, statusLed = 8;
    constexpr int cabColumnW = 238, cabCaptionGap = 10, cabCaptionH = 15, cabHelpH = 30;
    constexpr int selectionOffset = 3, selectionWidth = 3, selectionRadius = 9;          // selected tile outline
    constexpr int pedalInset = 15, pedalLed = 16, pedalLedY = 48, pedalNameY = 85, pedalNameH = 25;
    constexpr int footswitch = 55, footswitchY = 138, footswitchBorderW = 2;
    constexpr int pedalShadowY = 6, pedalShadowBlur = 10;
    constexpr int panelLabelBottom = 57, panelLabelH = 16;  // "AMP · NAM CAPTURE": top at height − 57
    constexpr int platePadX = 10, plateH = 20, plateRadius = 3, plateRight = 44, plateBottom = 38;
    constexpr int knobCell = 64, knobColumn = 74, knobLabelGap = 4, knobLabelH = 12, chipH = 13, chipPadX = 5, chipRadius = 3;
    constexpr int toggleW = 22, toggleH = 36, toggleKnob = 16, toggleColumn = 74, toggleColumnH = 91;
    constexpr int toggleOnH = 10, toggleTrackY = 14, toggleNameY = 54, toggleNameLineH = 12, toggleOffY = 80;
    constexpr int powerLed = 28, ledGlowR = 8, ledHalo = 54;
    constexpr int gearLabelMin = 8, gearLabelDivisor = 14;   // kind label size = height / 14, at least 8
    constexpr int scrollbar         = 10;    // track width; thumb inset 2 px, radius 6

    // Stage widgets
    constexpr int widgetRadius      = 6;
    constexpr int widgetTagInset    = 6;     // name tag at (6, 6)
    constexpr int stageListRow      = 48;    // set list widget rows at text scale 1
    constexpr int stageHeadingRow   = 30;
    // Read from the Stage board (V3Stage.dc.html; content sizes at the
    // reference size, scaled with the window and the widget's text size).
    constexpr int widgetTagPadX = 6, widgetTagH = 15;
    constexpr int stageGridPad = 8;          // around the widget grid (gap between widgets: `gap`)
    constexpr int stageToolbarPadX = 12, stageToolbarGap = 12, stageModeSegmentW = 72, stageButtonPadX = 14, stageMuteMinW = 84;
    constexpr int miniMetersW = 80, miniMeterBarH = 4, miniMeterGap = 3;
    constexpr int stageFooterPadX = 12, stageFooterGap = 16;
    constexpr int widgetTitleX = 22, widgetTitleY = 34;                       // "SONG 3   SET 1"
    constexpr int currentNameX = 18, currentNameBottom = 22;
    constexpr int widgetTextX = 16, widgetTextBottom = 10;                    // Patch, Next cue (Cue note: bottom 12)
    constexpr int cueNoteBottom = 12, cueNoteGap = 2, nextCueGap = 16;
    constexpr int healthX = 12, healthBottom = 8, clockBottom = 8;
    constexpr int lockGap = 6, lockPadTop = 14, lockIcon = 34, panicPadTop = 12, stageButtonPadTop = 10;
    constexpr int stageTriangleGap = 0;      // ◂ Back, NEXT ▸: the triangle's own box leaves the space
    constexpr float stageTriangle = 0.85f;    // the triangle's box, of the font's ascent
    constexpr float stageCapCentre = 0.375f; // the middle of the capitals above the baseline, of the ascent
    constexpr float stagePedalScale = 0.7f;
    constexpr float nextCuePatchShare = 0.3f;   // Next cue: the patch name keeps at least this share of the width (then it's cut)  // stomp pedals: 0.7 of 160 x 210 at the reference size
    constexpr int widgetTagRadius = 3, stageEmptyPadX = 8;
    constexpr int stompsX = 14, stompsRight = 10, stompsBottom = 10, stompsGap = 22, stageShadowH = 8;
    constexpr int barsTop = 34, barsBottom = 30, barsLabelBottom = 8, meterBarsGap = 8;
    constexpr int pedalTrackRadius = 4, meterBarRadius = 3;
    constexpr float pedalTrackInset = 0.30f, meterBarsInset = 0.18f;        // of the widget's width, each side
    constexpr int stageListX = 6, stageListTop = 30, stageListBottom = 6, stageListGap = 2;
    constexpr int stageListPadX = 12, stageListNumberW = 20, stageListCellGap = 12, stageHeadingPadBottom = 3;
    constexpr int stageCurrentBase = 140;    // the current cue's name at text size 1 (168 at the board's 1.2)
    // Layout editor (SCREENS.md §6, layout-editor.json)
    constexpr int layoutToolbarGap = 8, layoutSeparatorGap = 12, layoutHintGap = 14, layoutsButtonW = 150, layoutDoneW = 70;
    constexpr int separatorH = 22, inspectorToggleIcon = 15;
    constexpr int artboardMargin = 20, artboardInset = 3;           // around the preview; inside its border
    constexpr int widgetListTop = 8, widgetGroupH = 24, widgetGroupX = 11, widgetRowH = 30;
    constexpr int widgetChipX = 15, widgetChipW = 24, widgetChipH = 20, widgetChipRadius = 3, widgetNameX = 48, widgetListPadX = 7;
    constexpr int handle = 8, handleRadius = 2, handleGrab = 14, selectionBorder = 2, selectionHalo = 3;
    constexpr int layoutInspectorPad = 10, layoutLabelGap = 8, layoutButtonGap = 8, layoutHalfGap = 6;
    constexpr int layoutNotesH = 123, layoutNoteGap = 6;
    // Settings window (SCREENS.md §7, settings-*.json)
    constexpr int settingsW = 1040, settingsH = 760;
    constexpr int groupTitleH = 18, groupGap = 16, groupRadius = 7, groupPad = 15, settingsColumnGap = 16, settingsPadTop = 18;
    constexpr int audioLeftW = 664, audioRightW = 320, settingsLabelW = 130, settingsLabelGap = 12;
    constexpr int segmentHeight = 26, bufferSegmentW = 68;   // segmented controls in settings (+ 2 px border)
    constexpr int deviceRowGap = 10, devicePopupW = 340, ratePopupW = 140, latencyBarH = 8, latencyTextGap = 5;
    constexpr int channelColumnX = 325, checkRowH = 22, checkHeadGap = 7, statusBadgeMid = 26, statusRowH = 21;
    constexpr int statusKeyW = 146, statusDividerY = 54, statusRowsY = 65, tuningRowH = 23, tickSize = 9;
    constexpr int midiColumnW = 335, midiRightW = 300, midiPadTop = 16, midiInputRowH = 28, midiInputTop = 13;
    constexpr int mappingHeaderH = 27, mappingRowH = 31, mappingBarH = 37, mappingPadX = 12, actionW = 200;
    // Settings > Plugins (milestone 10): the plugin list where the MIDI tab has
    // its mappings (same header, rows and bar), folders and the blocklist on the right.
    constexpr int pluginsMakerW = 190, pluginsVersionW = 90, pluginsColumnGap = 10, pluginsFoldersBoxH = 147, pluginsBlockedRowH = 45;
    constexpr int pluginsRowH = 27, pluginsBarH = 41, pluginsTitleRowH = 20, pluginsStatusTop = 10, pluginsProgressTop = 29;
    constexpr int pluginsCountLift = 5, pluginsBlockedNameTop = 7, pluginsBlockedReasonTop = 24, pluginsTryAgainGap = 10, pluginsHelpPadTop = 10;
    // Milestones 10 and 11 (NEW-FEATURES-M10-M11 §C)
    constexpr int tagChipH = 16, tagChipRadius = 3, tagChipPadX = 6;
    constexpr int progressH = 4, progressRadius = 2;
    constexpr int triggerX = 222, learnW = 76, learnH = 22, removeW = 22, removeRight = 18, lastValueW = 117, lastValueH = 21;
    constexpr int helpLineH = 18, helpGap = 10, groupPadTight = 13;   // (help groups: 13 above and below the text)
    constexpr int statusTitleH = 15, statusLineH = 14, statusTextH = 15, latencySplit = 2, tickTextGap = 10, defaultsValueX = 170;
    constexpr float tickStroke = 1.5f;
    // Pre-show check (SCREENS.md §8, preshow-check.json)
    constexpr int preShowW = 840, preShowH = 780, summaryBandH = 81, summaryBadgeX = 22, summaryBadgeY = 18, summaryTextX = 82;
    constexpr int summaryTitleY = 19, summaryTitleH = 24, summarySubY = 46, summarySubH = 15, liveDot = 7, liveGap = 6;
    constexpr int plusTileStroke = 2;
    constexpr int showTitleMaxW = 160;
    constexpr int lcdCutMinW = 60;        // ...and further, down to this, when the toolbar is short of room
    constexpr int lcdNameMaxW = 320;      // a long cue or patch name in the LCD is cut here   // the show name in the toolbar: longer names are cut   // the dashed "+" tile border
    constexpr int summaryBadge = 44, liveAreaW = 60, liveTop = 5, liveKeyGap = 3, rowLineGap = 2;
    constexpr int checkListTop = 6, checkHeadingH = 26, preShowRowH = 59, rowBadge = 22, rowTextX = 62, checkPadX = 22, rowTitleH = 16;
    // Tuner overlay (the board's tuner pane)
    constexpr int tunerPaneW = 900, tunerPad = 24, tunerGap = 18, tunerLcdPad = 22, tunerLcdGap = 16;
    constexpr int tunerNote = 180, tunerTicksH = 52, tunerTickMid = 38, tunerTickShort = 26, tunerTickGap = 6, tunerTickRadius = 3;
    constexpr int tunerTicks = 21, tunerCentsPerTick = 5, tunerOctaveGap = 4;
    constexpr float tunerOctave = 0.3f, tunerOctaveRaise = 0.45f;   // the octave: size and top, of the note's
    constexpr int tunerButtonH = 60, tunerButtonPadX = 44, tunerButtonRadius = 8;

    // Notices
    constexpr int noticeBadge = 16, noticePadX = 12, noticeGap = 10;          // Edit notice bar (+ 1 px line = 31)
    constexpr int bannerPadX = 14, bannerGap = 12, spinner = 12, spinnerStroke = 2; // Stage strips (+ 1 px line = 35)

    // Meters
    constexpr int meterTrackH = 8, meterValueW = 54, meterLabelW = 50, meterGap = 8, meterScaleH = 11, meterPeakW = 2;

    // LCD
    // Heights and padding inside the 1 px border (the box is 34 / 38 tall); a
    // 1 px highlight line sits under the box, so the component is 35 / 39.
    constexpr int lcdRadius = 6, lcdEdit = 32, lcdStage = 36, lcdShadowH = 5;
    constexpr int lcdPadX = 14, lcdGap = 18, lcdPadXStage = 16, lcdGapStage = 20;
    constexpr int lcdMinPatch = 110, lcdMinPatchStage = 140;   // the name item keeps its width

    // Full list columns (CueListEditor): number, name 45 % of flex, bar, note (rest), patch, MIDI
    constexpr int colNumber = 64, colBar = 150, colPatch = 190, colMidi = 90;
    constexpr int indentPerLevel = 18;
    constexpr int cellPadX = 6, sectionPadX = 10, sectionKindW = 70, cellEditorPadX = 6, cellEditorH = 24, cellEditorInsetR = 4;
    constexpr int subToolbarPadX = 12, subToolbarGap = 6, subToolbarTitleW = 98, subToolbarButton = 70, subToolbarWideButton = 90;
    constexpr int backButtonW = 130, keyCapGap = 6, keyCapH = 14;
    constexpr int playingBar = 4;            // green bar on the playing row (3 px in pane lists)
}

//==============================================================================
// Type scale. Inter (bundled). Values are CSS px = em size.
// JUCE: FontOptions::withPointHeight (px) if it takes the em size in your JUCE
// version, else withHeight (px * emToJuceHeight). Verify once with the
// snapshot test in docs/design/JUCE-ARCHITECTURE.md §8.
namespace type
{
    constexpr float emToJuceHeight = 1.2099609f;   // Inter: (ascender 1984 + descender 494) / 2048

    enum class Weight { regular = 400, medium = 500, semibold = 600, bold = 700, extraBold = 800 };

    struct Style { float px; Weight weight; float tracking = 0.0f; bool uppercase = false; };

    constexpr float lcdLineHeight = 1.05f;   // LCD value / label lines (em)
    constexpr float titleLineHeight = 1.15f; // the show name over its save state
    constexpr float normalLine = 1.21f;      // Inter's "normal" line height (ascent + descent), em
    constexpr float minTextPx = 8.0f, shrinkStep = 0.9f;   // text that shrinks to fit: not below 8 px, 10 % a step

    // UI chrome
    constexpr Style body            { 12.5f, Weight::regular };             // rows, values
    constexpr Style bodySmall       { 12.0f, Weight::regular };             // fields, button text
    constexpr Style button          { 12.0f, Weight::medium };
    constexpr Style label           { 11.5f, Weight::regular };             // field labels ("k"), help
    constexpr Style caption         { 11.0f, Weight::regular };             // footers, notes
    constexpr Style paneHeader      { 10.0f, Weight::bold, 0.08f, true };   // "SET LIST", "INSPECTOR · AMP"
    constexpr Style lcdValue        { 14.0f, Weight::semibold };            // Edit LCD (16 on Stage)
    constexpr Style lcdLabel        {  8.5f, Weight::bold };                // Edit LCD labels (9 on Stage)
    constexpr Style toolbarTitle    { 12.5f, Weight::semibold };            // show name
    constexpr Style sectionHeading  { 10.5f, Weight::extraBold, 0.06f, true }; // sidebar SET 1
    constexpr Style tableRow        { 13.5f, Weight::regular };
    constexpr Style tableSection    { 14.0f, Weight::bold };
    constexpr Style tableKind       { 11.0f, Weight::bold, 0.06f, true };
    constexpr Style tableMidi       { 12.0f, Weight::regular };             // "PC 5  OUT"
    constexpr Style menuItem        { 12.5f, Weight::regular };
    constexpr Style menuShortcut    { 11.0f, Weight::regular };
    constexpr Style dialogTitle     { 12.5f, Weight::semibold };
    constexpr Style summaryLarge    { 20.0f, Weight::bold };                // pre-show summary
    constexpr Style checkHeading    { 10.0f, Weight::bold, 0.08f, true };   // PROBLEMS, TO LOOK AT, FINE
    constexpr Style checkTitle      { 13.0f, Weight::semibold };
    constexpr Style checkDetail     { 12.0f, Weight::regular };
    constexpr Style noticeText      { 12.5f, Weight::semibold };
    constexpr Style bannerText      { 15.0f, Weight::bold };                // stage banners

    // Added while building the design in JUCE (values read from the boards)
    constexpr Style paneHeaderNote  { 10.0f, Weight::medium };              // "6.9% CPU", "value · pedal"
    constexpr Style buttonSmall     { 10.5f, Weight::medium };              // 18 px pane buttons
    constexpr Style buttonBold      { 12.0f, Weight::bold };                // PANIC, MUTED, Mute
    constexpr Style lcdValueStage   { 16.0f, Weight::semibold };
    constexpr Style lcdLabelStage   {  9.0f, Weight::semibold };
    constexpr Style meterValue      { 11.0f, Weight::regular };
    constexpr Style meterScale      {  9.0f, Weight::regular };
    constexpr Style miniMeterLabel  {  9.0f, Weight::bold };                // Stage toolbar CPU / DROPOUTS
    constexpr Style rowStrong       { 12.5f, Weight::semibold };            // current row, show name
    constexpr Style rowTrailing     { 11.0f, Weight::regular };             // patch name at the end of a row
    constexpr Style saveState       { 10.5f, Weight::regular };             // "Saved 23:28"
    constexpr Style headerLine      { 14.0f, Weight::semibold };            // "Cue 1 of 8: Clean"
    constexpr Style healthTitle     { 11.5f, Weight::semibold };            // "System health"
    constexpr Style plusGlyph       { 22.0f, Weight::regular };             // "+" tile in the chain
    constexpr Style cabCaption      { 12.0f, Weight::bold };                // "Cab · <IR> (IR)" under the cab
    constexpr Style gearKind        {  8.0f, Weight::bold, 0.08f, true };   // AMP on chain tiles
    constexpr Style gearGroup       { 10.0f, Weight::extraBold, 0.16f, true }; // PREAMP / EQ / OUTPUT
    constexpr Style knobLabel       { 10.0f, Weight::extraBold, 0.12f, true }; // INPUT, BASS...
    constexpr Style toggleLabel     {  8.0f, Weight::extraBold };           // ON / OFF
    constexpr Style chip            {  9.5f, Weight::bold };                // "EXP 1"
    constexpr Style gearChip        {  9.0f, Weight::extraBold };           // "EXP 1" under a gear knob
    constexpr Style gearPanelKind   { 13.0f, Weight::bold, 1.0f / 13.0f, true }; // AMP · NAM CAPTURE on the amp panel (1 px)
    constexpr float gearBoxTrackingPx = 1.0f;                               // amp and cab kind labels: 1 px at any size
    constexpr Style plate           { 11.0f, Weight::extraBold, 0.12f, true }; // model plate "JC 120B"
    constexpr Style pedalName       { 22.0f, Weight::semibold };            // name on a 160 × 210 pedal
    constexpr Style toggleName      { 10.0f, Weight::extraBold, 0.08f, true }; // LEVEL MATCH
    constexpr Style keyCap          { 10.0f, Weight::regular };             // "F6"
    constexpr Style subToolbarTitle { 14.0f, Weight::bold };                // "Cue list"
    constexpr Style subtitle        { 12.0f, Weight::regular };             // summaries, help in dialogs
    constexpr Style groupTitle      { 10.0f, Weight::bold, 0.08f, true };   // dialog group boxes
    constexpr Style tabLabel        { 11.0f, Weight::medium };              // settings tabs
    constexpr Style noticeNote      { 11.0f, Weight::medium };              // notice bar, right-hand note
    constexpr Style lostNote        { 11.5f, Weight::medium };              // lost banner, right-hand note
    constexpr Style badgeMark       { 11.0f, Weight::extraBold };           // "!" in the notice badge
    constexpr Style statusMark      { 12.0f, Weight::extraBold };           // "!" in a 22 px status badge
    constexpr Style statusMarkLarge { 22.0f, Weight::extraBold };           // "!" in the 44 px summary badge
    constexpr Style mutedBar        { 12.0f, Weight::semibold };            // "Output muted - click to go live"

    // Stage (scaled by the widget's text size and the widget's height)
    constexpr Style widgetTag       { 10.5f, Weight::semibold };
    constexpr Style widgetTitle     { 13.0f, Weight::bold, 0.08f, true };
    constexpr Style currentName     { 168.0f, Weight::extraBold, -0.04f };  // fits the widget; shrink to fit
    constexpr Style stageListName   { 20.0f, Weight::bold };
    constexpr Style stageListHeading{ 13.0f, Weight::extraBold, 0.06f, true };
    constexpr Style stageListNumber { 13.0f, Weight::regular };
    constexpr Style stageListPatch  { 15.0f, Weight::regular };
    constexpr Style clock           { 52.0f, Weight::extraBold };
    constexpr Style healthMain      { 30.0f, Weight::extraBold };            // "CPU 7%", "AUDIO LOST"
    constexpr Style healthSub       { 12.0f, Weight::bold };                 // "OK · 0 dropouts · 5.3 ms"
    constexpr Style lockLabel       { 38.0f, Weight::extraBold };
    constexpr Style lockHint        { 14.0f, Weight::regular };
    constexpr Style panicLabel      { 50.0f, Weight::extraBold };
    constexpr Style panicHint       { 13.0f, Weight::regular };
    constexpr Style patchName       { 40.0f, Weight::extraBold };
    constexpr Style cueNote         { 18.0f, Weight::semibold };
    constexpr Style cueBar          { 13.0f, Weight::regular };
    constexpr Style nextCueName     { 44.0f, Weight::extraBold };
    constexpr Style nextCuePatch    { 18.0f, Weight::regular };
    constexpr Style backButton      { 26.0f, Weight::bold };
    constexpr Style nextButton      { 30.0f, Weight::extraBold };
    constexpr Style barLabel        { 12.0f, Weight::regular };              // "EXP 1 · 72%", IN, OUT
    constexpr Style stageEmpty      { 16.0f, Weight::regular };              // "No stomp switches in this patch"
    constexpr Style stageText       { 40.0f, Weight::semibold };             // Text widget (shrinks to fit)
    constexpr Style stageFooter     { 11.0f, Weight::regular };
    constexpr Style stageFooterAlert{ 11.0f, Weight::semibold };             // the lost text in the footer
    constexpr Style layoutName      { 11.0f, Weight::regular };              // "Layout “Default”" in the toolbar
    constexpr Style widgetChip      {  9.0f, Weight::bold };                // "CC", "SL" in the widget list
    constexpr Style widgetGroup     { 10.0f, Weight::bold, 0.08f, true };   // SHOW, PLAY, MONITOR, SAFETY
    constexpr Style widgetTagPreview{  8.5f, Weight::semibold };            // name tags on the layout artboard
    constexpr Style layoutHint      { 11.5f, Weight::regular };
    constexpr Style layoutNote      { 11.0f, Weight::regular };
    constexpr Style settingsLabel   { 12.0f, Weight::regular };             // "Driver type", status keys
    constexpr Style helpText        { 12.0f, Weight::regular };
    constexpr Style helpStrong      { 12.0f, Weight::bold };
    constexpr Style statusTitle     { 12.5f, Weight::semibold };
    constexpr Style noteText        { 11.5f, Weight::regular };             // notes under groups, the latency line
    constexpr Style noteStrong      { 11.5f, Weight::bold };
    constexpr Style columnHead      { 10.5f, Weight::bold, 0.06f, true };   // DOES, FOOTSWITCH / PEDAL
    constexpr Style tagChip         { 9.5f, Weight::bold, 0.02f, true };    // NOT HERE, OUTPUT MUTED, counts
    constexpr Style tunerCaption    { 11.0f, Weight::bold, 0.08f, true };
    constexpr Style tunerNote       { 180.0f, Weight::semibold };
    constexpr Style tunerDetail     { 18.0f, Weight::regular };
    constexpr Style tunerButton     { 16.0f, Weight::semibold };
}

//==============================================================================
// Interaction states (DESIGN-SYSTEM.md §9)
namespace state
{
    constexpr float hoverLighten  = 0.06f;   // raised controls get 6 % lighter on hover
    constexpr float activeLighten = 0.15f;   // a stage button widget while it acts (panic silencing)
    constexpr float disabledAlpha = 0.45f;
    constexpr float cutAlpha      = 0.35f;   // cut items in the full list
    // Summary badge for warning / ok (the boards only draw the problem one):
    // the badge colour lit and darkened the way the problem badge is.
    constexpr float badgeLight = 0.4f, badgeRim = 0.5f, badgeGlowAlpha = 0.45f;
}

//==============================================================================
// Motion. A live UI does not animate: state changes are instant. The only
// moving things are meters, the tuner and the reconnect spinner, all driven by
// the existing 30 Hz UI timer (MainComponent::timerCallback). Keep LevelMeter's
// existing ballistics (fall-back and peak hold) unchanged.
namespace motion
{
    constexpr int uiTimerHz = 30;
}

} // namespace encore::theme
