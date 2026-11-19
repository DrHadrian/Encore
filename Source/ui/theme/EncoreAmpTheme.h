// Encore — live guitar rig host
// Copyright (C) 2026 the Encore contributors. Licensed under the GNU AGPLv3 (see LICENSE.md).
//
// Design tokens of the amp faces and the amp builder (theme::amp): kept apart
// from EncoreTheme.h so that only the files that draw amps (kit::AmpFace,
// kit::AmpFaceView, ui::AmpBuilderView) rebuild when one changes. Same rules
// as EncoreTheme.h: no literal in amp code outside this file.

#pragma once

#include "ui/theme/EncoreTheme.h"

namespace encore::theme
{

//==============================================================================
// Amp faces (design/AMP-BUILDER.md, board BAmpBuilder): the materials and
// print of amps built in the amp builder. They are drawn things (metal,
// rubber, light), not UI chrome, so they live apart from the UI colours;
// composed once to an image per amp (kit::AmpFace).
namespace amp
{
    constexpr int faceW = 600, faceH = 430;   // the GearView amp box (Edit)
    constexpr int snap = 2;                   // the builder's grid (px)
    constexpr int guideReach = 6;             // smart guides: centres within 6 px line up
    constexpr juce::uint32 guide = 0xffFF4FD8;   // the smart guides' magenta

    // Print on the panels: text, ticks, symbols, frame lines (and title rules).
    constexpr juce::uint32 printDark   = 0xff141416, ruleWhite = 0xff3A3A3E, ruleOrange = 0xff3A2410;   // white, orange panels
    constexpr juce::uint32 printGold   = 0xff4A3A1C, ruleGold  = 0xff6A5630;
    constexpr juce::uint32 printPlexi  = 0xff2A2112, rulePlexi = 0xff5A4628;
    constexpr juce::uint32 printSilver = 0xff2A2A2E, ruleSilver = 0xff55555B;
    constexpr juce::uint32 printLight  = 0xffE8E8EB, ruleBlack = 0xff6A6A70, ruleDark = 0xff8A8A90;  // black, dark steel
    constexpr juce::uint32 printOnMetal = 0xff2A2112;   // on a gold or silver panel section

    // Panel sections: their edges.
    constexpr juce::uint32 sectionShine = 0x2EFFFFFF, sectionShade = 0x80000000, sectionRing = 0xB3000000, sectionDrop = 0x80000000;
    constexpr int sectionRadius = 4;

    // Feet (head bodies), studs, corners.
    constexpr juce::uint32 footTop = 0xff1E1E20, footBottom = 0xff0A0A0B, footShine = 0x14FFFFFF;
    constexpr int footW = 86, footH = 18, footRadius = 8, footLeftX = 64, footRightX = 450, footY = 400;
    constexpr juce::uint32 studLight = 0xffD9C9A0, studMid = 0xff6E5A36, studDark = 0xff2A2216;
    constexpr int studSize = 5, studPitch = 26, cornerSize = 22;

    // Knobs: shadow; the plain black knob; the pointer line.
    constexpr juce::uint32 knobShadow = 0x8C000000;
    // A knob's CSS drop-shadow: Y = max(2, s/18) down, blur B = max(1, s/30) (sigma B/2), black 55 %, every style.
    constexpr float knobShadowMinY = 2.0f, knobShadowYDiv = 18.0f, knobShadowMinBlur = 1.0f, knobShadowBlurDiv = 30.0f;
    constexpr int knobShadowCacheMax = 256;   // shadow images kept (per look, size, frame, pixel scale)
    constexpr juce::uint32 plainLight = 0xff4A4A4F, plainMid = 0xff1C1C1F, plainDark = 0xff0B0B0C, plainRing = 0xff050506, plainShine = 0x40FFFFFF;
    constexpr juce::uint32 pointer = 0xffF2F2F4, pointerShade = 0x99000000;

    // Push button (out / pressed in).
    constexpr juce::uint32 pushCapLight = 0xff4E4E53, pushCapMid = 0xff1B1B1E, pushCapDark = 0xff0A0A0B, pushGap = 0xff2A2A2E,
                           pushChromeLight = 0xffF4F4F6, pushChromeMid = 0xffA4A4AA, pushChromeDark = 0xff45454A, pushEdge = 0xff1A1A1C;
    constexpr juce::uint32 pushInCap = 0xff18181A, pushInCapMid = 0xff0C0C0E, pushInCapDark = 0xff040405, pushInGap = 0xff6A6A70,
                           pushInChrome = 0xffE4E4E8, pushInChromeMid = 0xff9A9AA0, pushInChromeDark = 0xff3A3A3E, pushInEdge = 0xff161618;
    constexpr juce::uint32 switchShadow = 0x99000000;

    // Lights: the glow around a lit pilot jewel; the LEDs (lit centre, rim, unlit).
    constexpr juce::uint32 glowRed = 0xB3FF6E3C, glowAmber = 0xB3FFAA28, glowGreen = 0x9950E65A, glowBlue = 0x995096FF;
    constexpr juce::uint32 ledRed[3]   { 0xffFF6A58, 0xffB0221A, 0xff3A1512 };
    constexpr juce::uint32 ledAmber[3] { 0xffFFC85A, 0xffB06A10, 0xff3A2810 };
    constexpr juce::uint32 ledGreen[3] { 0xff6CF58C, 0xff169A3A, 0xff13301A };
    constexpr juce::uint32 ledBlue[3]  { 0xff7DBBFF, 0xff1D5AAE, 0xff142236 };
    constexpr juce::uint32 ledHot = 0xffFFFFFF, ledUnlitShine = 0x59FFFFFF, ledUnlitEdge = 0xff0A0A0B, ledBezelInner = 0xff0A0A0B, ledBezel = 0xff7A7A80;

    // Jack, screw.
    constexpr juce::uint32 jackHole = 0xff050506, jackLight = 0xffD9D9DE, jackMid = 0xff8A8A90, jackDark = 0xff3A3A3E, jackEdge = 0xff18181A, jackShadow = 0xB3000000;
    constexpr juce::uint32 screwLight = 0xffF2F2F4, screwDark = 0xff7A7A80, screwShadow = 0x99000000;

    // Panel handle: chrome bar and its two caps.
    constexpr juce::uint32 handleDark = 0xff6A6A70, handleShine = 0xffF6F6F8, handleMid = 0xffA8A8AE, handleEdge = 0xff4A4A50, handleShadow = 0x80000000;
    constexpr juce::uint32 handleCapTop = 0xffD9D9DE, handleCapBottom = 0xff6A6A70;

    // Name plates: gold, silver, black, black with a gold rim (text colour last).
    constexpr juce::uint32 plateGoldTop = 0xffF1DFA8, plateGoldBottom = 0xffB8913E, plateGoldText = 0xff2A2112, plateGoldShine = 0x99FFFFFF;
    constexpr juce::uint32 plateSilverTop = 0xffF2F2F4, plateSilverBottom = 0xffA8A8AE, plateSilverText = 0xff1A1A1C, plateSilverShine = 0xffFFFFFF;
    constexpr juce::uint32 plateBlackTop = 0xff2A2A2E, plateBlackBottom = 0xff121214, plateBlackText = 0xffD9D9DE, plateBlackShine = 0x26FFFFFF;
    constexpr juce::uint32 plateRimCentre = 0xff2A2A2E, plateRimEdge = 0xff0E0E10, plateRimText = 0xffD9B865, plateRim = 0xffC9A452, plateRimInner = 0xff3A2E14;
    constexpr juce::uint32 plateShadow = 0x99000000, plateRimShadow = 0xB3000000;

    // Trim strips.
    constexpr juce::uint32 trimGold[4]   { 0xffF6E3A8, 0xffB8913E, 0xff6E5420, 0xffD9B865 };
    constexpr juce::uint32 trimSilver[4] { 0xffFFFFFF, 0xffB9B9BF, 0xff5A5A60, 0xffD9D9DE };
    constexpr juce::uint32 trimBlackTop = 0xff3A3A3E, trimBlackBottom = 0xff0E0E10;

    // The Encore logo's colours; the badge's fill.
    constexpr juce::uint32 logoCream = 0xffF3E6C4, logoWhite = 0xffF2F2F4, logoGold = 0xffD9B865, logoBlack = 0xff1A1A1C;
    constexpr juce::uint32 badgeFill = 0x59FFFFFF;

    // The builder's marks on parts: what a knob drives, NOT SET (amber), selection.
    constexpr juce::uint32 bindDrivesFill = 0xff1B2C45, bindDrivesText = 0xff7FC4FF;
    constexpr juce::uint32 bindFixedFill = 0xff2C2C30, bindFixedText = 0xffC4C4C9;
    constexpr juce::uint32 bindNotSetFill = 0xff4A3510, bindNotSetText = 0xffFFD27A;
    constexpr juce::uint32 selectedOutline = 0xff6EA5FA, notSetOutline = 0xffFFB340, handleFill = 0xffFFFFFF, handleBorder = 0xff2E6FDC;

    // The amp builder (board BAmpBuilder): the face's work area, notes in the inspector.
    constexpr juce::uint32 workArea = 0xff121418, workDots = 0x12FFFFFF, faceShadow = 0x8C000000;
    constexpr juce::uint32 dropFill = 0x294C8DF6;    // where a dragged part lands: accent at 16 %
    constexpr juce::uint32 notSetNoteFill = 0xff2A251C, notSetNoteBorder = 0xff3D2F12, notSetNoteText = 0xffFFD27A;
    constexpr juce::uint32 fixedNoteFill = 0xff232326;
    constexpr juce::uint32 swatchRing = 0xffE8E8EB, swatchShine = 0x0FFFFFFF;   // selected ring (CSS outline), inner ring
    // Tolex swatches: flat colours, the tweed as stripes (board TOLEX).
    constexpr juce::uint32 tolexBlack = 0xff1C1B1A, tolexOrange = 0xffC8601E, tolexWhite = 0xffE0DED6, tolexCream = 0xffD6C6A0,
                           tolexBrown = 0xff4E3220, tolexRed = 0xff6E1C18, tolexBlonde = 0xffC4A470;
    constexpr juce::uint32 tweedDark = 0xff7A5228, tweedLight = 0xffD9B878;
    constexpr float tweedPeriod = 3.4f, tweedLine = 1.0f;   // repeating-linear-gradient(135deg, dark 0 1px, light 1px 3.4px)
    constexpr float pipingNoneBand = 0.12f;                 // the None slash: 44 % .. 56 % of the 135° gradient line
    // Swatches the builder can't cut from the layers: piping cords, light colours.
    constexpr juce::uint32 pipingGold = 0xffD6C080, pipingWhite = 0xffE4E2DC, pipingBlack = 0xff1E1E20, pipingNone = 0xff2A2A2E, pipingNoneSlash = 0xffFF8A80;
    constexpr juce::uint32 lightRed = 0xffFF6E3C, lightAmber = 0xffFFAA28, lightGreen = 0xff50E65A, lightBlue = 0xff5096FF;

    constexpr int partsW = 220, inspectorW = 300, footerH = 30, ampPickerW = 210, backButtonW = 156;
    constexpr int dotPitch = 16, faceShadowY = 24, faceShadowBlur = 20;
    constexpr int handleSize = 7, bindH = 16, bindPadX = 6, outlineGap = 3;
    constexpr int rowLabelW = 86, rowGap = 8, rowMinH = 26, inspectorPadX = 12, inspectorPadY = 10, swatch = 22;
    constexpr int headerH = 25;                                               // the builder's column headers: .hd 24 + its 1 px line
    constexpr int knobPictureSize = 24, knobPictureButtonH = 34;              // the Knob choice: 24 px pictures in 34 px buttons
    constexpr int fieldH = 24, fieldPairGap = 6, fieldPadX = 8, buttonsRowTop = 6;   // Duplicate | Remove: 6 px above                              // Position / Size: two fields, "x 278"
    constexpr int rowGapY = 6;                                                // the inspector's rows: a column with gap 6
    constexpr float noteSize = 11.0f, noteLineH = 1.5f, filledNoteSize = 11.5f, filledNoteLineH = 1.45f;
    constexpr int notePadX = 10, notePadY = 8, noteRadius = 6, closingNoteTop = 4;
    constexpr int sectionLineTop = 10, sectionLineBottom = 6, sectionTitleH = 12, sectionTitlePadBottom = 2;   // the line and "SOUND"
    constexpr int choiceH = 24, drivesH = 22;                                  // option buttons: Template, Body (24), Drives (22)
    constexpr int swatchGap = 6, swatchRadius = 5;                          // .ab-sw: 6 px across and down
    constexpr int swatchRingOut = 2, swatchRingRadius = 7, swatchRingW = 2;  // outline 2 px, offset 1: stroke centred 2 px out
    constexpr int swatchMargin = 3;                                          // room for the ring around a swatch group
    constexpr int labelTopBody = 4, labelTopSymbol = 6, labelLineH = 14;     // Body / Symbol labels sit at the top
    constexpr int dropBorder = 2, dropRadius = 6, gapLabelH = 12;
    constexpr int undoDepth = 100;
}

} // namespace encore::theme
