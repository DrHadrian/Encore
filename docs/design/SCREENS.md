# Encore screens (design v3) — what to build, screen by screen

Each section names the target image(s) in `reference/target/`, the exact geometry source in `reference/geometry/`, the existing code it replaces or restyles, and every string it shows. Coordinates are at the 1440 × 900 reference size; §2 says how screens adapt to other sizes. Component looks are in `DESIGN-SYSTEM.md`; this file is about layout, content and behaviour.

**Repository state this was written against:** `drhadrian/encore` main @ 49983b4 (milestone 9) plus PR #11 / branch `milestone-8` @ f3be14a (theatre cue lists, "Set list / Cue list"). If milestone 8 isn't merged yet when you start, merge or rebase on it first: the Edit sidebar, full list, go-to and Stage titles depend on it.

---

## 0. Reading the boards

- **Data on the boards is example data** (song names, "Phantom — tour 2027", "Focusrite USB ASIO", CPU 7 %). Real screens show the real show and device.
- **The Stage boards show one possible layout.** Stage content is data: the widgets and their grid positions come from the show's `StageLayout` (24 × 16 grid, `makeDefaultStageLayout()` for new shows). Build every widget type as specified in §3.3 and let the layout place them. (The reference "default" arrangement differs slightly from `makeDefaultStageLayout()` — Patch and Cue note side by side, Back/Next buttons added. Don't change the model's default to match the picture.)
- **Not in the app, do not build** (they're proposals that slipped onto a board): the search field and drag-to-canvas in the layout editor's widget panel; the "Big monitor · cue 3 only" layout is only example data for the per-cue layout feature (which *does* exist). Everything else on the v3 boards exists in code.
- **Nothing may be lost.** Every control, menu item, key, tooltip and message the app has today must still exist after the conversion (the parity map, §12, lists where each one goes). Where a board doesn't show something the app has (block status lights, "Loading...", problem text under the chain, file choosers, small dialogs), style it with the kit and put it where §12 says.

---

## 1. Words: Set list or Cue list

One running order; `ShowSession::getWords()` (`ShowModel.h`, `wordsFor()`) gives the words for the show's kind (Show → Kind of show):

| Kind (`ShowTemplate`) | `list` | `item` / `items` | new section |
|---|---|---|---|
| band, worship, solo | Set list | song / songs | set ("New set") |
| musical | Cue list | cue / cues | scene ("New scene") |

Use them for: the sidebar title, "+ Song" / "+ Cue", the full list's title and item column header, menu items ("Go to this song", "MIDI out when this cue starts..."), the Stage current-item title ("SONG 3", "CUE 5"), LCD labels ("SONG"/"CUE"), the go-to prompt ("Go to cue 8_"). Keep "Cue" where the code says Cue regardless of kind: the Edit chain line "Cue 3 of 12: …", the widget type names ("Current cue", "Next cue", "Cue note and bar", "Set list"), the full list's tooltips. Re-read the words on every refresh (the kind can change while a screen is open).

---

## 2. Window and frame

- `MainWindow` keeps the native title bar (`setUsingNativeTitleBar (true)`), background `window` #161618 (currently #121417 — change it).
- **Edit view** (`MainComponent`): designed at 1440 × 900; works from 1180 × 720 (`theme::size::minEditWidth/Height`). Columns: sidebar 250 fixed, inspector 272 fixed, centre flexible; bottom row 104 fixed; the centre block area takes the remaining height. Below the minimum the existing `ScrollingContent` viewport scrolls (keep it; style its scrollbar). Keep `setResizeLimits (640, 480, …)` — small windows scroll, they don't break.
- **Stage view** (`StageView`): fills the window at any size (it already does); the widget grid scales, toolbar and footer keep their heights.
- **High DPI:** all sizes are logical px; JUCE scales them. Never multiply by the display scale yourself. Images (icons, logo) are vectors; cached images are rendered at `Graphics::getInternalContext().getPhysicalPixelScaleFactor()`.
- **Dialogs** (Audio/MIDI settings, pre-show check) are `DialogWindow`s today; they become frameless child windows drawn with `DialogFrame` over a dimmer, centred on the main window, same sizes as the boards (Settings 1040 × 760, Pre-show 840 × 780; shrink to the window minus 40 px when smaller). Keep them modeless where they are modeless today (they are opened with `launchAsync` and the app keeps running), keep Escape = close, keep the SafePointers that close them before the things they edit go away.

---

## 3. Stage

Targets: `stage.png`, `stage-musical-goto.png`, `stage-interface-lost.png` (+ `@2x`). Geometry: `stage.json`, `stage-musical-goto.json`, `stage-interface-lost.json`. Code: `ui/StageView.*`, `StageSnapshot`, `MainComponent::setStageVisible/timerCallback`.

### 3.1 Toolbar (48 px + line) — replaces the 30 px top strip
Left → right, 12 px gaps, 12 px padding:
1. Logo wordmark (98 × 17).
2. Mode switch **Stage | Edit | Layout** (segments 72 × 30, Stage selected). Edit = `onLeave` (today's "Edit view" button), Layout = `setEditingLayout (true)` (today's "Edit layout"). Tooltips from today's buttons.
3. "Layout “Default”" — 11 px #85858C, the layout being shown (`current.name`).
4. Flexible space, **LCD** (36 px high): item number with label `SONG`/`CUE` (16 / 600 value, 9 / 600 label) and item name with label = section path in caps (e.g. "SET 1", "ACT 1 / REHEARSAL"; `getSectionPath`) — min 140 px wide. Flexible space.
5. Mini meters (80 px wide): `CPU` with % and a 4 px bar (green #30C25A), `DROPOUTS` with the count.
6. **Lock** (not on the board, required for parity): when the layout has no Lock widget, a 36 px icon button with `icons/lock.svg` sits here; when locked it becomes the wide "LOCKED: hold to unlock" button (today's `lockStripButton`, same text and hold-progress text) and every other toolbar control is disabled.
7. **Tuner** button (36 high, padding 14).
8. **Mute / Muted** (36 high, min 84): "Mute" in #FF8A80 on the default button when live; "Muted" on the MUTED gradient when muted.

### 3.2 Strips under the toolbar (stacked, in this order, only while active)
- **Lost banner** (34): "AUDIO INTERFACE LOST - reconnecting to <device>..." + spinner; right: "Staying on cue n · retrying every second · check the cable". Text = `snapshot.audioProblem`.
- **Notice strip** (34, amber): `snapshot.notice` (crash-restart text, "Audio interface reconnected", go-to prompt "Go to cue 8_   (Enter to go, Esc to cancel)", "There's no cue 14"). Hidden when empty.
- **Muted bar** (28): "Output muted — click to go live"; click = unmute.

### 3.3 Widget grid
Area below the strips and above the footer, padding 8, gap 8 (today 4 — change `gap`), 24 × 16 cells from `StageLayout`. Each widget = `WidgetFrame` (DESIGN-SYSTEM §5.11) + content. Content sizes below are at the reference size and scale with the widget height and its `textScale` (Small 0.7, Normal 1.0, Large 1.4, Huge 2.0, Biggest 3.0 — the values already in `StageView`). Text that doesn't fit shrinks to fit (keep today's fit-to-width logic), never ellipsises mid-word on Stage except list rows.

| Widget (tag) | Content |
|---|---|
| Current cue | title `ITEM n   SECTION / PATH` (13 / 700 caps #6E7480, at 22,34); name 800 weight, as large as fits (168 px at 947 × 300), left 18, bottom 22. Before the first cue: "Ready"; no cues: chain label or "No cues". |
| Next cue | title "NEXT"; main "n  Name" (44 / 800) and the patch name after it (18 #8E8E95) on one baseline; "End" after the last. |
| Patch | title "PATCH"; patch name 40 / 800. |
| Cue note and bar | title = bar mark (or "NOTE"); note 18 / 600; |
| Set list | window of the running order around the current item (starts one row above it, plus its heading if that's directly above; cut items left out — today's logic): headings 30 px rows, 13 / 800 caps amber 85 %, bottom-aligned; items 48 px rows radius 4: number 13 (#7A7A80; white when current), name 20 / 700, patch 15 right (#85858C; #D3E1FA when current); current row #2E62C4; played rows text #7A7A80. Tap an item = go to it. |
| Stomp switches | the playing patch's stomps as pedal placeholders (DESIGN-SYSTEM §8), 22 px apart, bottom-aligned, on the chain-board fill; tap toggles (`toggleStomp`), LED shows state. |
| Expression pedals | one vertical bar per pedal (track #2A2D33, fill #6EA5FA → #3A7AE6), label "EXP 1 · 72%" 11 px #8E8E95 under it. |
| Level meters | IN and OUT vertical bars (green to 50 %, yellow above, track #2A2D33), labels 11 px. |
| Clock | "23:31" 52 / 800 tabular, centred at the bottom. |
| System health | "CPU 7 %" 30 / 800 and "OK · 0 dropouts · 5.3 ms" 12 / 700 #30C25A; "DROPOUT!" / "ON BATTERY" in amber on warning; **AUDIO LOST**: whole widget on the panic gradient, title "System", main "AUDIO LOST" white, sub "reconnecting..." #FFD3CE (no glow). |
| Lock button | lock icon 34, "LOCK" 26 / 800, hint "click to lock · hold 1 s to unlock" 11 #8E8E95; locked: shows the hold progress text. |
| Panic button | panic gradient, "PANIC" 50 / 800 white, "silence & reset all" 13 #F3C4C0. |
| Next button | primary gradient, "NEXT ▸" 30 / 800 white. |
| Previous button | default widget, "◂ Back" 26 / 700 #C4C4C9. |
| Tuner | the tuner display (§3.5) inside the widget. |
| Text | the widget's text, centred, 600, as large as fits. |

### 3.4 Footer (22 px)
Fill #1B1B1E, top line #0A0A0B, 11 px #85858C, 12 px padding, 16 px gaps: device line ("ASIO · 48 kHz · 64 samples · 5.3 ms round trip"; while lost, the red lost text), footswitch status ("Footswitch connected"), flexible space, "Lock: Ctrl+L · hold the lock 1 s to unlock · F11 full screen".

### 3.5 Tuner overlay (no tuner widget on the layout)
Full grid area, fill #0E0E10 at 97 %, centred LCD-styled panel: big note, cents bar (21 ticks, centre tick taller, in-tune tick #30C25A), Hz, "A = 440 Hz", **Done** button (also T, Esc, click). Restyle `TunerDisplay`; keep its logic and readability rules.

### 3.6 Layout edit mode
See §6 (it's the Layout editor screen).

### 3.7 Keys (unchanged)
Space / → / ↓ next, ← / ↑ previous, digits + Enter go to, T tuner, Ctrl+L lock, F11 full screen, F5 Stage ↔ Edit, Esc closes the tuner.

---

## 4. Edit

Targets: `edit.png`, `edit-recovered-show-menu.png`. Geometry: `edit.json`, `edit-recovered-show-menu.json`. Code: `MainComponent` (layout, toolbar, status), `SetList`, `PatchList`, `ChainStrip`, `BlockPanel`, `LevelMeter`.

### 4.1 Toolbar (44 + line), left → right (10 px gaps, padding 10)
Logo · **Show ▾** (opens §10.1; pressed look while open) · show name (12.5 / 600) over save state (10.5 #85858C: "Saved 23:28" or "Unsaved changes (autosaves)" — today's strings, today shows seconds: keep `%H:%M:%S`) · mode switch **Stage | Edit | Layout** (64 × 26, Edit selected; Stage = `setStageVisible (true)`, Layout = stage + `setEditingLayout (true)`) · flexible · **LCD** (32 high): `n / N` + `SONG`/`CUE`; current item name + `PATCH <NAME>`; round trip ms + `ROUND TRIP`; rate "48.0k" + `RATE` · flexible · **Tuner** · **MIDI...** · **Audio...** (today "Audio settings..." — the shorter label is the design) · `Out` label + volume slider (70 × 4 track, 12 px thumb, value field 66 wide "−12.0 dB") · **PANIC** (#FF8A80, 700) · **MUTED/LIVE** (74 wide).

### 4.2 Notice bar (30, only while a notice is showing)
`showNotice()` text (crash restart, reconnected, go-to prompt, "There's no …"), "!" badge, right note optional. Today the notice goes into the `warning` label: move it here; the ASIO warning stays in the health pane.

### 4.3 Main grid (padding 8, gap 8): sidebar 250 | centre | inspector 272

**Sidebar, top half: running order** (`SetList`), pane with header:
- header label = `words.list` ("Set list"); right: **Full list** small button (tooltip "The full list: sections, bar marks, notes, cut items, fast typing (F6)"), then icon buttons + − ↑ ↓ = today's "+ Add" (tooltip "New song after the playing one"), "Remove", "Up", "Down" (same enable rules).
- rows 26 px (today 38): playing bar 3 × 16 (#30C25A on the playing row), number 18 px #7A7A80, name (12.5, 600 on the current row), right: "PC n  " / "OUT  " + patch name (11 px #85858C; "(patch missing)" in amber); current row #2E62C4.
- section headings 22 px, amber caps; cut items hidden.
- click = go to; double-click = rename (section → open full list); right-click = today's menu (§10.3).
- keep today's tooltip text.

**Sidebar, bottom half: Patches** (`PatchList`): header "PATCHES", icon buttons + (New), duplicate, trash (Delete; disabled with one patch). Rows 26: 9 px colour square (patch colour), name (600 when selected), selected row #2B2B30. Footer note 11 px #85858C: "A patch can be used by several cues. Editing it changes every cue that uses it." Double-click rename, right-click menu §10.4, tooltip unchanged.

**Centre:**
1. Header line (28): "Cue 1 of 8: Clean" (14 / 600; `chainLabel`'s first part), arrow-right icon, patch picker field "Patch: Clean ▾" (130 wide; opens the patch list as a menu → `setCuePatch`). "(loading patches...)" appears after it in #85858C while building. Problems text (`problemsLabel`, amber 11.5) right-aligned on this line when present.
2. **Signal chain** pane: header "SIGNAL CHAIN" + right text "Click a block to edit it · right-click to move, insert, remove or give it a stomp switch" (0 tracking, 500). Body: chain board (#19191B, inset shadow), padding 14 × 12, tiles bottom-aligned with 6 px gaps (§8 of DESIGN-SYSTEM: amp 117 × 84, cab 64 × 84, other blocks 64 × 84, + 48 × 84). Off (bypassed) blocks: LED off. Selected: blue outline. Click/right-click as today (`ChainStrip`).
3. **Block stage** pane (fills the rest): header with a 7 px green dot (block on) or grey (off), "AMP · <model name>" (caps; block type + loaded file's name), right: "6.9% CPU" and **Load amp...** / **Load cab IR...** small button (the block's `FileSlot` button text). Body: radial-lit background (radial #3A3A3E → #1C1C1F → #141416 from 45 % / 25 %), the selected block's **gear placeholder panel** centred at the bottom (amp 600 × 430 + cab 230 × 300 beside it when the selected block is the amp and a cab follows; other blocks: their own placeholder panel 600 × 300 with the same control strip), knobs and switches built from the block's parameter list (as `BlockPanel` does today), group labels for the amp (PREAMP: Input · EQ: Bass, Mid, Treble · OUTPUT: Level match, Output, power LED). Status light (e.g. gate open) = a small LED + label on the strip. Knob drag, double-click reset, right-click "Expression pedal (heel = min, toe = max) ▸" exactly as today.
   - While the tuner is open in Edit (`editTuner`), it covers this centre column (today it covers the body), styled as §3.5.

**Inspector (right, 272):** pane "INSPECTOR · AMP" + "6.9% CPU" (block CPU). Sections with pane-header-style subheaders (transparent background, line above):
- **BLOCK**: two-column grid (74 px labels 11.5 #8E8E95, values 12): Type ("Amp (NAM capture)"), Model (field with the file name, ellipsised; "(none)" / "Loading..."), Format (the file info text, e.g. "48.0 kHz · standard · ~12% CPU"), Stomp switch (popup: None, Stomp 1–8 → same action as ChainStrip's "Stomp switch" menu), on/off parameters as checkboxes (e.g. "Level match ☑ On").
- **PARAMETERS** (right: "value · pedal"): one 26 px row per continuous parameter: name 60 px #C4C4C9, 4 px value bar (#8E8E95; #6EA5FA when a pedal moves it), value 54 px right-aligned tabular, 44 px chip "EXP n" (#1B2C45 / #7FC4FF) or empty. Read-only mirror of the knobs (exact values live here).
- **ASSIGNMENTS** (right: "right-click a knob or block"): "Stomp n → <block> on/off", "EXP n → <parameter> · heel <min>, toe <max>" for the selected patch.

### 4.4 Bottom row (104), same columns as the grid
- Left pane: "Guitar input" label, input popup ("1: Input channel 1"; `inputChoice`), device line 11 px #85858C ("<device> · <driver type>").
- Centre pane: Input and Output meters (`LevelMeter` restyled, §5.7), value right ("−9.4 dB"; "muted" in #FF8A80 when muted), dB scale under them.
- Right pane: "System health" (11.5 / 600 #8E8E95) + **Reset counters** small button; then 11 px #A9A9AF lines: health ("CPU 7% · overruns 0 · dropouts 1 · limiter idle"), device/latency ("48.0 kHz · 480 samples · 20.0 ms round trip"), and the warning line in amber ("⚠ Not using ASIO — pick an ASIO driver for the stage"; while lost: the red lost text; today's Windows/battery line goes here too). Today's five status labels collapse into these three lines; tooltips on each line give the full original text so nothing is lost.

---

## 5. Full list (F6)

Targets: `full-list-typing.png`, `full-list-item-menu.png`. Geometry: same names. Code: `ui/CueListEditor.*`, `CueDialogs`.

Covers the whole main grid (sidebar + centre + inspector: x 8–1432, y 53–780 at the reference size); toolbar, notice bar and bottom row stay. One pane:
- **Toolbar** (40, sub-toolbar gradient, line below): title `words.list` (14 / 700, 98 wide) · **+ Cue/+ Song** (70) · **+ Section** (90) · **Cut**/**Restore** (70; disabled on sections) · **Delete** · **Up** · **Down** · **Go to** (70 each, 6 px gaps) · summary right-aligned 12 #8E8E95 ("10 cues, 1 cut, 6 sections") · **Back to patches** + key cap "F6" (130). Tooltips: today's.
- **Column header** (24): `#` (right-aligned) · item word caps · BAR · NOTE · PATCH · MIDI. Widths: 64 · 45 % of (width − 494) · 150 · rest · 190 · 90 (today's `columnBounds`).
- **Rows** (28, 13.5 px): see DESIGN-SYSTEM §5.5 and §9. Section rows: kind caps amber 11 / 700 in a 70 px column, name 14 / 700 white, indent 18 × depth. Item rows: number right-aligned (white 70 %), name indented, bar mark amber, note white 75 %, patch white 60 % ("(own)", "(missing)"), MIDI 12 px "PC 5  OUT". Playing: 4 px green bar + bold. Selected: accent 35 %. Cut: "cut", 35 %, strike-through.
- **Typing:** in-place editor over the cell (DESIGN-SYSTEM §5.3 field, editing variant). Behaviour exactly as `CueListEditor` today (Enter/Tab/Shift+Tab/Esc, chaining, commit on focus loss).
- Menus §10.5/§10.6, delete confirmation §11.

---

## 6. Layout editor (Stage → Layout)

Target: `layout-editor.png` (`layout-editor.json`). Code: `StageView` edit mode (`setEditingLayout`, `Editor`, `showLayoutMenu`, `showAddWidgetMenu`, widget right-click menu).

- **Toolbar (44):** logo · mode switch (Layout selected) · **Layout: Default ▾** (150; opens the layouts menu §10.7; pressed while open) · **+ Widget** (opens the add-widget menu at the next free cell, as today) · hint 11.5 #85858C ("Drag to move, drag the edges to resize, right-click for options, double-click empty space to add") · flexible · **Inspector** toggle (sidebar-right icon; pressed when on; key I) · **Done** (primary, 70).
- **Left panel (220):** pane "+ WIDGET" / "click to add". Groups (10 / 700 caps #6E6E75): Show (Current cue, Next cue, Patch, Cue note and bar, Set list, Text), Play (Next button, Previous button, Stomp switches, Expression pedals), Monitor (Level meters, System health, Clock, Tuner), Safety (Panic button, Lock button). Rows 30: 22 × 18 icon chip (#2A2D34, border #3A3E47, 9 / 700 #B9BEC9: CC, NC, P, N, SL, TX, ▸, ◂, SW, EX, LM, SH, CL, TU, !, LK), name 12.5 #E1E1E5, hover #2A2A2F. Click = add that widget at the next free cell (same function as the menu). (No search field.)
- **Centre:** pane "STAGE LAYOUT · 60%" (the preview scale) + the app's hint on the right. The stage grid drawn as an artboard (aspect of the window), centred, widgets as on Stage at preview scale, cell grid lines faint (#FFFFFF 4 %). Selected widget: blue border + halo + handles (right, bottom, corner). Drag/resize/snap/double-click empty to add, right-click menu §10.8 — all as today.
- **Right panel, widget inspector (280):** shows while a widget is selected and the Inspector toggle is on (remember the toggle per computer in `AppSettings`). Pane "WIDGET · <type>" + "selected" (#4C8DF6). Text size segmented (Small, Normal, Large, Huge, Biggest); **Edit text...** (enabled for Text widgets only); **Duplicate** / **Bring to front**; **Remove** (#FF8A80 text). Notes 11 #8E8E95: "Widgets snap to the 24 × 16 grid and scale with the window, so one layout works on a laptop and on a big monitor." and "Same options as the right-click menu. Shows while a widget is selected; hide it with Inspector (I) in the top bar." Off or nothing selected → the centre takes the full width.
- **Status bar (22):** "14 widgets · 24 × 16 grid", "Selected: Current cue", right: "Changes save with the show" (11 #85858C).

---

## 7. Settings: Audio and MIDI (one window, two tabs)

Targets: `settings-audio.png`, `settings-midi.png` (geometry same names). Code: `MainComponent::openAudioSettings` (`AudioDeviceSelectorComponent`), `MidiSettings`, `system::TuningReport`. Opened by **Audio...** / **MIDI...**; the pre-show check's fix buttons open the matching tab.

Frame: `DialogFrame` 1040 × 760 ("Settings — Audio" / "Settings — MIDI"), tabs Audio | MIDI (78 × 54) under the title, footer: help text left, **Cancel** and **Done** (84). Today these dialogs apply changes immediately: keep that; **Done** and **Cancel** both close (Cancel does not undo — the footer text says changes apply immediately). Escape closes.

Where a board's wording differs from the code's ("…" vs "...", shortened labels), the code's string wins unless this file names the change (e.g. "Audio...").

**Audio tab** (left column 664 wide, right 320, group boxes):
- DEVICE group: label column 130 right-aligned 12 #A9A9AF. Driver type: segmented "ASIO | Windows Audio" (the device types JUCE offers). Device: popup (340) + **Control panel** button (sliders icon) shown only if `device->hasControlPanel()`; it calls `showControlPanel()` then refreshes. Sample rate popup (140). Buffer size: segmented of the sizes the device offers (show at most the 5 common ones 32/64/128/256/480 plus the current if different; more in a popup). Latency: bar (in / out) + text "In 2.6 ms + Out 2.7 ms = 5.3 ms round trip · shown in the main window", round trip 700 #30C25A.
- ACTIVE CHANNELS group: Inputs / Outputs checklists; note "Which input is the guitar is chosen under Guitar input in the main window."
- WINDOWS LIVE TUNING · AUTOMATIC group (read-only ✓ / ✕ from `TuningReport` and `isAudioThreadProAudio()`): Power throttling off, High process priority, Pro Audio audio thread, Computer and screen kept awake; "Encore always starts muted, and a safety limiter protects the output."
- STATUS group (right): ✓ badge + driver + "device · 48 kHz · 64 samples"; key/value rows CPU, Overruns, Driver dropouts, Limiter, Memory, Latency (data from today's health line); **Reset counters**.
- TIP group: "Use 64 samples for the stage. If you hear clicks, go up one step — Encore shows dropouts here and in the System health widget on stage."
- Footer: "Changes apply immediately; audio restarts for less than a second when you change the device."

Implementation note: don't embed JUCE's stock `AudioDeviceSelectorComponent` look. Build the tab from the `AudioDeviceManager` API (types, devices, rates, buffer sizes, channels; `setAudioDeviceSetup`) so it looks like the board, keeping exactly what today's selector lets you change.

**MIDI tab** (`MidiSettings` restyled; columns 335 / 335 / 300):
- LISTEN TO: input checkboxes (unavailable devices #8E8E95).
- SEND CUE MESSAGES TO: output popup + help "Program and control changes each cue sends — to other gear. Set them per cue in Edit."
- MAPPINGS (full width of the first two columns): "This show has its own mappings (instead of this computer's)" checkbox on the header line; table header "Does" / "Footswitch / pedal" (10.5 / 700 #6E6E75); rows 31 px: action popup (200; menu as in `MidiSettings`: Next cue, Previous cue, Program recall, Stomp switch ▸ 1–8, Expression pedal ▸ 1–4, Mute, Tuner, Panic — the code still appends " (from milestone 7)" to Tuner and Panic; that suffix is stale and may be dropped), trigger text 12.5 #D5D5DA ("CC 64, any channel"), **Learn** (76) / **Cancel** (primary) while learning, with "Press the control now..." in amber, ✕ remove (22). Bottom: **+ Add mapping**, **Reset to defaults**, "Last received" + value field.
- HOW MAPPINGS WORK and DEFAULTS help groups (right column, text from the board).
- Footer: "Mappings save automatically to this computer (%APPDATA%\Encore), never into the show — unless you choose “This show”."

---

## 8. Pre-show check (F9, Show → Pre-show check...)

Target: `preshow-check.png`. Code: `PreShowCheckView`, `PreShowCheck`. `DialogFrame` 840 × 780 titled "Pre-show check".
- Summary band (≈ 80): 44 px status badge, summary 20 / 700 ("N problem(s) to fix before the show" / "Ready, with N thing(s) to look at" / "Ready for the show"), sub line 12 #A9A9AF ("1 problem · 4 to look at · 3 fine · checks again every second, so you can fix things and watch them turn green"), right: green "Live" dot + "F9". Band tint follows the worst status (red: #2A1E1E → #241C1D; amber and green equivalents at the same darkness).
- Rows grouped under "PROBLEMS" / "TO LOOK AT" / "FINE" (10 / 700 caps, red/amber/green): min 46 px, 22 px badge, title 13 / 600 (problems #FF8A80), detail 12 #A9A9AF, fix button right (primary on problems): Audio settings..., MIDI settings..., Save now, Reset counters, Unmute.
- Footer (48): "Worst first. Lines with a button can be fixed from here. The log is in Show → Open log folder."
- Re-checks every second (existing timer); rows update in place without rebuilding components when nothing changed.

---

## 9. Notices, banners, recovery

- Crash restart: notice "Encore restarted after an unexpected exit - back at cue <n name>" (+ ". MUTED: it happened again soon after, check before unmuting.") for 30 s; target `edit-recovered-show-menu.png` shows the bar.
- Reconnected: "Audio interface reconnected", 10 s.
- Interface lost: Stage §3.2 banner + health widget; Edit: red device line in the health pane.
- Go to: digits anywhere → notice "Go to cue 8_   (Enter to go, Esc to cancel)"; "There's no cue 14" for 4 s.

---

## 10. Menus (all `PopupMenu`, styled by the LookAndFeel; texts exactly as in code)

Use `PopupMenu::Item::shortcutKeyDescription` for shortcuts the code writes in brackets ("Save now (Ctrl+S)" → "Save now" + shortcut "Ctrl+S"; same for F9, G, Enter, Ctrl+Up/Down). Ticks are drawn by the LookAndFeel.

1. **Show**: New show... · Open show... | Save now (Ctrl+S) · Save a copy to... (USB stick, backup) | Show folder in Explorer · Kind of show ▸ (Band  (Set list) · Musical / theatre  (Cue list) · Worship  (Set list) · Solo  (Set list)) | Pre-show check... (F9) · Open log folder | Stage settings (this show) ▸ (Tuner mutes the output · Concert pitch ▸ A = 415 … 446 Hz · Panic ▸ Silence, clear tails, then come back by itself / Silence until panic is pressed again) | Open backups folder.
2. **Patch picker** (centre header): the show's patches, current ticked.
3. **Running order row (sidebar)**: Go to this song · Rename... · Patch ▸ · If the previous song uses the same patch ▸ (Keep playing as it is / Reset to its saved settings) | Recall with a program change... · MIDI out when this song starts... | Remove.
4. **Patch row**: Rename... · Duplicate · Fade in when switched to (20 ms) ▸ · Amp ring-out when switched away (150 ms) ▸ · Then let ring out ▸ (Delay and reverb ring out (lighter on CPU) / The whole patch rings out) | Delete... (disabled with one patch).
5. **Full list, item**: Go to this cue (G) · Rename (Enter) · Bar mark · Note · Patch... | Recall with a program change... · MIDI out when this cue starts... | Cut (skip in the show) / Restore (uncut) | Move up (Ctrl+Up) · Move down (Ctrl+Down) · Delete...
6. **Full list, section**: Rename · Kind ▸ (Act, Scene, Set, Song, Number, Section) · New cue at the start of this section | Move up (Ctrl+Up) · Move down (Ctrl+Down) · Delete...
7. **Layouts** (Layout: … ▾): section header (today "Edit layout"; design "Layouts in this show") · the show's layouts (current ticked; design adds "show's default" / "cue n only" notes right-aligned) | New layout (copy of this one)... · Rename... · Use as the show's default · Use for cue <n “name”> only / Cue <n>: back to the default layout | Delete this layout (danger; disabled when only one) · Reset to the built-in layout.
8. **Widget (right-click)**: section header = widget type · Text size ▸ (Small … Biggest) · Edit text... (Text only) · Duplicate · Bring to front · Remove.
9. **Add widget**: the 16 widget types (`describe (type)`).
10. **Chain block**: Move left · Move right | Stomp switch ▸ (None, Stomp 1–8) | Insert before ▸ · Insert after ▸ (block types) | Remove. **+ tile**: block types.
11. **Knob**: Expression pedal (heel = min, toe = max) ▸ (None, Expression 1–4).

---

## 11. Small dialogs (AlertWindow)

Rename (song/patch/layout/text), New show ("Name of the show (band, production, set...):", Create / Cancel), delete confirmations ("Delete \"Clean\"?" + "The patch is removed from this show. A backup of the show is kept."; "Delete cue 5 \"Title Song\"?"; "Delete the section \"Rooftop\" (its cues stay)?"), program change, MIDI out, errors ("Couldn't create the show", "Couldn't load <file>", "MIDI out not changed"). Keep them as `AlertWindow`s but drawn by the LookAndFeel (`drawAlertBox`, `getAlertWindowButtonHeight` 24, fonts, fields per DESIGN-SYSTEM): `DialogFrame` look at a compact size (min 360 wide), title in the title bar, message 12.5, text editors as inset fields, buttons right-aligned (the confirming button primary; destructive "Delete" uses danger text). File choosers stay native.

---

## 12. Parity map (today's control → where it is in the design)

| Today (`MainComponent` unless noted) | Design |
|---|---|
| `title` label "Encore" | logo in the toolbar |
| `showButton` "Show" | Show ▾ |
| `showName`, `saveState` | toolbar, after Show |
| `stageButton` "Stage" | mode switch: Stage |
| `tunerButton` | Tuner (Edit toolbar, Stage toolbar) |
| `panicButton` | PANIC (Edit toolbar); Panic widget on Stage |
| `midiSettingsButton` | MIDI... → Settings, MIDI tab |
| `audioSettingsButton` | Audio... → Settings, Audio tab |
| `volume` slider | Out slider + dB field |
| `muteButton` MUTED/LIVE | MUTED/LIVE (Edit), Mute/Muted (Stage) |
| `setList` (+ Add, Remove, Up, Down, Full list) | sidebar running order pane |
| `patchList` (+ New, Duplicate, Delete) | sidebar Patches pane |
| `chainLabel` | centre header line + LCD |
| `problemsLabel` | centre header line, right, amber |
| `chainStrip` | Signal chain pane |
| `blockPanel` (title, power, cpu, file row, knobs, switches, status light) | Block stage pane header (title, CPU, Load...), gear placeholder (knobs, switches, LED, status light), inspector (type, model, format, stomp switch, parameters) |
| `editTuner` | tuner over the centre column |
| `cueEditor` | Full list |
| `inputLabel`, `inputChoice` | bottom-left pane |
| `inputMeter`, `outputMeter` | bottom-centre pane |
| `resetCountersButton` | health pane (and Settings → Audio status) |
| `healthStatus`, `deviceStatus`, `latencyStatus`, `systemStatus`, `warning` | health pane lines (+ tooltips with the full text); notices → notice bar |
| StageView `editViewButton`, `editLayoutButton` | mode switch Edit / Layout |
| StageView `lockStripButton` | toolbar lock button / LOCKED button (§3.1) and the Lock widget |
| StageView `noticeLabel` | stage strips (§3.2) |
| StageView `layoutsButton`, `addWidgetButton`, `hint`, `doneButton` | layout editor toolbar |
| StageView widget right-click | widget menu + widget inspector |
| `PreShowCheckView` | Pre-show check dialog |
| `MidiSettings` | Settings, MIDI tab |
| Audio `DialogWindow` with `AudioDeviceSelectorComponent` | Settings, Audio tab |
| `TunerDisplay` | tuner overlay / widget / Edit tuner |
