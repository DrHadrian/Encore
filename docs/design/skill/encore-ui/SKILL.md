---
name: encore-ui
description: Use for any UI work in the Encore repo (drhadrian/encore, C++/JUCE): building or changing screens, settings, stage widgets, menus, dialogs or block controls, so they follow the Encore design system (design v3) natively in JUCE.
---

# Encore UI (design v3, native JUCE)

Encore's UI follows one design system. The full kit is in `docs/design/` (start with `docs/design/README.md`). This skill is the short version; the docs win if they differ.

## Before you change UI
1. Read `docs/design/DESIGN-SYSTEM.md` (look), `docs/design/SCREENS.md` (screens, strings, parity map) and `docs/design/JUCE-ARCHITECTURE.md` (structure, performance, robustness).
2. Open the target image for the screen in `docs/design/reference/target/` and its measurements in `docs/design/reference/geometry/<screen>.json`.
3. For a new feature, check `docs/design/PROPOSALS.md` (it may already be designed) and follow `docs/design/EXTENDING.md`.

## Rules
- **Native JUCE only**: `Graphics`, `Component`, `LookAndFeel`. Never a web view, HTML/CSS/JS, React or a new UI library. The `.dc.html` boards are drawings to read values from, never code to port.
- **Tokens only**: colours, sizes, fonts come from `Source/ui/theme/EncoreTheme.h` (+ `EncoreFonts`). No literals in screens. New value → new named token.
- **Kit only**: compose `Source/ui/kit` components (Pane, Toolbar, Lcd, SegmentedControl, IconButton, NoticeBar, DialogFrame, MeterBar, StatusBadge, KeyCap, GearPlaceholder/GearView, KnobView) and `EncoreLookAndFeel`. Missing piece → add a generic kit component.
- **Inter, bundled**; uppercase labels tracked; tabular numbers; icons as cached paths from `Resources/ui/icons`; the logo is the wordmark SVG, never typed.
- **Behaviour is sacred**: never drop or change an action, key, menu item, tooltip or message. Words from the code; running-order words from `ShowSession::getWords()`.
- **Nothing pretends**: no control without real behaviour behind it.
- **Gear is placeholders** (DESIGN-SYSTEM §8) behind `GearView` until the gear-art feature exists.
- **Performance**: no animation; repaint only on change; nothing allocated or built in `paint()`; cache gradients/paths/shadows; opaque where possible; the existing 30 Hz timer drives updates (no new timers); `ListBox` for growing lists.
- **Audio safety**: UI on the message thread; never block or allocate on the audio thread; soak test stays at zero dropouts.
- **Robustness**: SafePointer in async lambdas; LookAndFeel/fonts outlive users; works with no device, no MIDI, empty or huge shows, long names, 640×480–4K, 100–200 % scaling; keyboard focus stays with the show (decision 0025).

## Key numbers
Window #161618 · pane #1D1D20 border #0A0A0B radius 6 · pane header 24 px #252528→#202023, 10 px bold caps #85858C · toolbar 44 (Stage 48) #333337→#28282B · buttons 24 px radius 5 #4A4A4F→#3A3A3E, pressed #252528→#2E2E31, primary #4C8DF6→#2E6FDC · selection #2E62C4, accent #4C8DF6 · healthy #30C25A · amber #FFB340 · danger text #FF8A80, MUTED #E8483E→#BF2C25, panic #A62B24→#761A15 · LCD #1C232C→#12171D text #D5E4F7 · stage widget #1C1E23 border #2B2E35 · rows 26 (full list 28) · menus 24 px items, #2C2C30, highlight #2E62C4 · text 12–12.5 px Inter, labels 11.5, captions 11.

## Check your work
Add the screen to `Tests/UiSnapshotTests.cpp`, render with `ENCORE_UI_SNAPSHOTS=<dir>`, compare with `python docs/design/tools/ui-compare.py <dir> docs/design/reference/target <out>`; layout, sizes, colours and words must match. Build and run `EncoreTests` (incl. soak) before committing.
