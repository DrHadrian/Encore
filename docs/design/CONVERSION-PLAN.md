# Conversion plan: design v3 → Encore (C++ / JUCE)

Work in this order on a branch (e.g. `design-v3`), one commit (or PR) per step. **After every step the app builds, runs, plays and passes `EncoreTests`** — never leave a half-styled screen that loses a function. Each step ends with its snapshot(s) compared to `reference/target/` (`tools/ui-compare.py`) and its checklist ticked.

Base: main with milestone 8 (PR #11) merged.

---

## Step 1 — Foundation
- Copy `theme/EncoreTheme.h` to `Source/ui/theme/`; add `EncoreFonts`, `Icons`, `EncoreLookAndFeel`.
- Copy `assets/` to `Resources/ui/`; `juce_add_binary_data`; app icon in `juce_add_gui_app`.
- Set the LookAndFeel on the main window (and `LookAndFeel::setDefaultLookAndFeel` so dialogs and menus get it). Window background #161618.
- Add `UiSnapshotTests` with the font calibration check (JUCE-ARCHITECTURE §8).
- ✔ Every stock control in the app already looks closer (fonts, buttons, menus, scrollbars). Nothing moved yet.

## Step 2 — Kit
- `Pane`, `Toolbar`, `Lcd`, `SegmentedControl`, `IconButton`, `NoticeBar`, `DialogFrame`, `MeterBar`, `StatusBadge`, `KeyCap`, `GearPlaceholder`, `KnobView`.
- A debug-only "kit gallery" component (behind a command-line flag or test) rendering every component in every state → snapshot `kit.png` for review.

## Step 3 — Edit frame
- Toolbar (logo, Show, name/save state, mode switch, LCD, Tuner, MIDI..., Audio..., Out, PANIC, MUTED/LIVE).
- Notice bar; main grid columns; bottom row (input, meters, health pane). Status labels → health lines with full-text tooltips.
- ✔ `edit.png` frame matches; every old top-bar function reachable.

## Step 4 — Sidebar
- Running order pane (`SetList`): header with Full list + icon buttons, 26 px rows, headings, PC/OUT/patch, menus.
- Patches pane (`PatchList`).
- ✔ sidebar of `edit.png`; band and musical words both correct.

## Step 5 — Chain and block stage
- Centre header line (chain label, patch picker, problems text).
- Signal chain pane (`ChainStrip` repainted with gear placeholders, board, selection outline, + tile).
- Block stage pane (`BlockPanel` split): header (dot, title, CPU, Load...), gear placeholder panel with knobs/toggles/LED, group labels for the amp; Edit tuner over the centre.
- ✔ centre of `edit.png` (placeholders), all knob/menu/file behaviour unchanged.

## Step 6 — Inspector
- New `Inspector` column: BLOCK / PARAMETERS / ASSIGNMENTS for the selected block and patch.
- ✔ right column of `edit.png`.

## Step 7 — Full list
- `CueListEditor`: sub-toolbar, header, rows, sections, typing editor, menus; covers the main grid.
- ✔ `full-list-typing.png`, `full-list-item-menu.png`.

## Step 8 — Stage
- `StageView`: toolbar (logo, mode switch, layout name, LCD, mini meters, lock, Tuner, Mute), strips (lost, notice, muted), widget frames and every widget type, footer, tuner overlay, gap 8.
- ✔ `stage.png`, `stage-musical-goto.png`, `stage-interface-lost.png` at 1440 × 900, and sane at 1024 × 640 and 3840 × 2160.

## Step 9 — Layout editor
- Toolbar, widget panel, artboard canvas with handles, widget inspector (+ Inspector toggle remembered in `AppSettings`), status bar.
- ✔ `layout-editor.png`.

## Step 10 — Settings window
- `SettingsWindow` with Audio (new `AudioSettingsView` on `AudioDeviceManager`) and MIDI (`MidiSettings` restyled) tabs; Control panel button; pre-show fixes open the right tab.
- ✔ `settings-audio.png`, `settings-midi.png`.

## Step 11 — Pre-show check, notices, dialogs
- `PreShowCheckView` in a `DialogFrame`; recovery/reconnect/go-to notices; Show menu shortcuts; AlertWindow styling.
- ✔ `preshow-check.png`, `edit-recovered-show-menu.png`.

## Step 12 — Hardening pass
- Run the edge cases (JUCE-ARCHITECTURE §7) and budgets (§6); fix what fails.
- Update `README.md` (screens, licences: Inter OFL) and `docs/DECISIONS.md` (one decision: "UI is native JUCE with a token-driven LookAndFeel; gear art behind GearView").

---

## Final acceptance checklist

**Look**
- [ ] Every target board has a matching snapshot; side-by-side shows the same layout, sizes, colours, fonts and words.
- [ ] Inter everywhere; tabular numbers; uppercase labels tracked; no system font anywhere.
- [ ] Same button / field / menu / scrollbar look on every screen and dialog.
- [ ] Gear is placeholders with the exact boxes of the future art.

**Behaviour**
- [ ] Every item of the parity map (`SCREENS.md` §12) works as before.
- [ ] Every key (F5, F6, F9, F11, T, Ctrl+L, Ctrl+S, Space/arrows, digits + Enter, Esc, I in the layout editor, the full list's keys) works as before.
- [ ] Every menu item and tooltip from the code is present with the code's text.
- [ ] Band and musical shows use the right words everywhere.

**Robustness and speed**
- [ ] No audio device / no MIDI / empty show / 300 cues / long names / 640 × 480 to 4K / 100–200 % scaling all fine.
- [ ] Idle Edit < 1 % CPU; Stage < 2 %; no paint over 4 ms; soak test passes with zero dropouts.
- [ ] No allocations in `paint()`; no new timers; repaints only on change (checked with repaint debugging).
- [ ] No web view, no HTML/CSS/JS shipped, no new UI dependencies.
