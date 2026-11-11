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
    constexpr juce::uint32 selectionHalo   = 0x404C8DF6;   // accent at 25 %
    constexpr juce::uint32 learningRow     = 0x1FFFB340;   // MIDI row being learned (amber 12 %)

    // Patch colours (PatchModel::colour is used as stored; these are the defaults shown in the designs)
    constexpr juce::uint32 patchBlue       = 0xff4C8DF6;
    constexpr juce::uint32 patchOrange     = 0xffFF9F0A;
    constexpr juce::uint32 patchGreen      = 0xff30C25A;
    constexpr juce::uint32 patchRed        = 0xffFF6B4A;
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
    constexpr int listPad = 4, listGap = 1, rowPadX = 8, rowGap = 8, rowNumberW = 18, paneBarW = 3, paneBarH = 16;
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
    constexpr Style menuItem        { 12.5f, Weight::regular };
    constexpr Style menuShortcut    { 11.0f, Weight::regular };
    constexpr Style dialogTitle     { 12.5f, Weight::semibold };
    constexpr Style summaryLarge    { 20.0f, Weight::bold };                // pre-show summary
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
}

//==============================================================================
// Interaction states (DESIGN-SYSTEM.md §9)
namespace state
{
    constexpr float hoverLighten  = 0.06f;   // raised controls get 6 % lighter on hover
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
