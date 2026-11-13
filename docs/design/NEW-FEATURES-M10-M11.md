# Milestones 10 and 11 in design v3: screens and new kit components

**Status (2026-11-12).** Written in the design chat after milestone 10 (plugins, branch `milestone-10` @ 6dc4f54) and for milestone 11 (routing, `claude/MILESTONE-11-PLAN.md`). It adds to `SCREENS.md` and `DESIGN-SYSTEM.md`; where they differ on these features, this file wins until both are merged into them in the repo kit (`docs/design/`).

**Boards** (Design canvas "Encore — Live & Touch UI Redesign"):
- Page **v3 — Current features (milestones 8 to 10)**, row "Milestone 10 — plugins": V3EditPlugin, V3EditPluginOff, V3Plugins, V3PluginWindow. V3Audio, V3Midi and V3PreShow were updated (Plugins tab, driver-type popup, plugin line).
- Page **v3 — Milestone 11: routing (to build)**: M11RoutingIO, M11RoutingSends, M11Stage, M11Sends, M11SendsRows, M11Merge, M11Input, M11BottomRow, M11PreShow, M11Midi.

**Reference images and measurements** for every screen and state below: `docs/design/reference/REFERENCES.md` (target PNGs at 1× and 2×, geometry JSON, boards). In the repo kit this file sits at `docs/design/NEW-FEATURES-M10-M11.md`.

Rules as always: words from the code (for M11, from §0 of the plan), nothing pretends, no animation, no pop-ups on Stage, Mute stays where it is. Gear art stays placeholders. Every new piece below is a generic kit component (EXTENDING.md), with tokens in `EncoreTheme.h`.

---

## A. Milestone 10: plugins (built; restyle to these boards)

### A.1 Settings › Plugins (V3Plugins)
Replaces "no reference board yet" in SCREENS §7. Same frame, tabs Audio | MIDI | **Plugins** (icon `plug`), footer text from `SettingsWindow` ("Plugins belong to this computer. Each new one is tried in a separate process first, so a crash can't take Encore down."), Cancel / Done.
- **Left column (the MAPPINGS place), group "Plugins"**: header line also carries a hint right-aligned, 11.5 #85858C: "Add one to a patch with the + in the signal chain › Plugin" (small UI gain). Table: column heads NAME / MAKER / VERSION (10.5 / 700 caps #6E6E75 on #1E1E21, 26 high), columns `1fr · 190 · 90`, rows 26, alternate rows `mappingRowAlt`, selected row #2E62C4. Empty: "No plugins yet. Scan looks in the folders on the right." centred.
- **Bar (40, #1E1E21, line above)**: **Scan** / **Stop** (84), **Scan all again** (disabled while scanning, as in code), then the status (`getScanStatus()`: "Looking for plugins...", "Scanning 14 of 62: ValhallaSupermassive.vst3", "Scan stopped", "12 plugins, 3 blocked").
- **Small UI gain: ProgressBar** under the status while scanning (4 px track #0E0E10, fill accent #4C8DF6, radius 2). Needs a `PluginCatalog` getter for (index, total) next to the status text; hidden when not scanning. No animation: it moves only when the count changes.
- **Right column 300**: group "Folders" (26 px rows, selected #2E62C4; bar **+ Add folder...**, **Remove** (enabled with a selection), **Defaults**). Group "Blocked" with an amber count chip on its title line; 44 px rows: file name 12.5, reason 11 amber (ellipsised, full text in the tooltip), **Try again** (small); empty "Nothing blocked". Help line under the rows, 11.5 #85858C: "Scan skips blocked files. Try again unblocks one, so the next Scan tries it in a separate process."

### A.2 Edit with a plugin block (V3EditPlugin)
- **PluginTile** in the chain (64 × 84, utility tile look, see C.2): label VST3, status dot (green glow when running, amber when not loaded or switched off, grey off), the plugin's name in two lines max (10.5 / 700), maker as caption, a small footswitch shape at the bottom.
- **Block stage**: header "PLUGIN · VST3 · VALHALLAROOM" (the code's `panelInfo` kind + name), right "2.1% CPU" and **Open plugin window...** (window icon). Body: the placeholder plate (600 × 370): VST3 chip, name 16 / 700, "maker · v2.0.0" 12 #85858C, a recessed panel with the first 8 parameters as knobs in two rows of four with their plugin text under each (10.5 tabular), "+ N more in the plugin window" bottom-left, "PLUGIN · VST3" bottom-right.
- **Inspector · Plugin**: BLOCK rows Type "Plugin", Plugin "ValhallaRoom · Valhalla DSP", Format, Status ("Running" green), **Open plugin window...**, Stomp switch popup. PARAMETERS (right "value · pedal") as for other blocks, then "+ N more in the plugin window". ASSIGNMENTS as for other blocks.
- **Add block menu** (+ tile): Amp (NAM) · Cab (IR) · Noise gate · EQ · Delay | **Plugin ▸** → makers (▸ each, "Other" for none) | Find plugins... ; "No plugins found" (disabled) when the catalog is empty. Exactly `ChainStrip::blockTypeMenu`.

### A.3 A plugin switched off (V3EditPluginOff)
- **Notice bar** (amber, SCREENS §4.2): "Clean: ValhallaRoom took longer than the audio buffer and was switched off" (20 s, the code's text).
- Tile: amber border, amber dot, amber name. A plugin that isn't installed looks the same (second tile on the board).
- Block stage: amber header dot, no CPU text (the code clears it on a problem). **Small UI gain: FaultCard** over the dimmed plate (45 % opacity): 460 wide, #2A251C, border #3D2F12, warn icon amber, text #FFD27A 12.5: "**Switched off:** it took longer than the audio buffer 3 times in a row. The signal passes through it dry, so the show goes on." + **Turn back on** (primary; "Load again" for a crash) and **Open plugin window...**. The card's first sentence is the fault text; the 3-in-a-row and dry pass are true (`overrunsBeforeSwitchingOff = 3`; a switched-off plugin passes the dry signal). For "not installed" the card shows the load error and no Turn back on.
- Inspector Status in amber (wraps to 3 lines), **Turn back on** primary above **Open plugin window...**.

### A.4 Plugin window (V3PluginWindow)
Native window (`setUsingNativeTitleBar (true)`, close button only), title "ValhallaRoom — Clean". When the plugin has its own editor it shows that editor, untouched. **Small UI gain: ParameterList** in place of JUCE's `GenericAudioProcessorEditor` when it has none:
- header strip 40 (toolbar gradient): VST3 chip, name 13 / 700, "has no window of its own: its parameters" 12 #85858C, right "N parameters".
- rows 32: name 130 #C4C4C9, a horizontal slider (4 px track #0E0E10, fill #6E6E75, 12 px thumb as the volume slider), value 90 right-aligned tabular (the plugin's own text); alternate rows #1C1C1F. A ListBox (many parameters).
- footer 30: "Changes save with the patch. The window comes back when the patch rebuilds." (both true: `PluginBlock::getState`, `pluginWindowsToReopen`).

### A.5 Other screens
- **Settings › Audio**: Driver type is a **popup** (340 wide, `devicePopupW`), not a segmented control (code since 6dc4f54). Board V3Audio updated.
- **Settings › MIDI**: three tabs (Audio | MIDI | Plugins).
- **Pre-show check**: problem "N plugin(s) not playing", detail "The signal passes through them dry: Lead: H9 Shimmer isn't installed on this computer", fix **Plugin settings...** (primary).

---

## B. Milestone 11: routing (to build)
Decisions and backend: `claude/MILESTONE-11-PLAN.md` §0 and §3. Words: **Output**, **Send**, **Path A / Path B**, **Routing**; missing names: chip **NOT HERE** (amber).

### B.1 Edit centre header: Routing button
On the centre header line, right of the patch picker, right-aligned: **Routing** button with the route icon and key cap **F7** (pressed look while the Routing view is open). Tooltip "Inputs, outputs and every patch's sends (F7)". (The toolbar was too crowded for it.) F7 toggles, like F6 for the full list.

### B.2 Routing view (M11RoutingIO, M11RoutingSends)
Covers the main grid like the full list (SCREENS §5); toolbar, notice bar, bottom row stay. One pane:
- **Sub-toolbar (40)**: title "Routing" · segmented **Inputs & outputs** | **Sends**, each with a badge (THIS COMPUTER #1B2C45/#7FC4FF; THIS SHOW green) · flexible · **Back to patches** + key cap F7 (156).
- **Inputs & outputs** (left column ≈ 520, right column the rest):
  - INPUTS group: rows name (field) · channel popup (the interface's channel names) · horizontal meter (56) · remove; **+ Add input** (up to 8).
  - INTERFACE group: device, driver, channels in/out (read-only, "Audio..." to change).
  - NAMES THIS SHOW USES: each name with a ✓ (here) or NOT HERE + **+ Add input / + Add output**.
  - OUTPUTS group: per row name · channels popup (amber with "needs output 5" when missing) · level fader + dB field (66) · **M** mute (MuteButton) · stereo meter · EQ thumbnail (EqCurve, mini) · delay "0.0 ms ≈ 0.00 m"; **+ Add output** (up to 8); the selected row #2E62C4 tint.
  - Detail of the selected output: EQ (EqCurve 100 % width + 6 SmallKnobs: Low cut, Bass, Mid, Mid freq, Treble, High cut), Delay (field "12.0 ms" with − / + and "≈ 4.12 m"), "Safety limiter at −1 dBFS · always on" with its activity LED.
  - Footer: "Inputs and outputs belong to this computer. Shows use the names, so a show plays right on another interface."
- **Sends**: SendMatrix: rows = patches (name, input, end "mono"/"stereo"), columns = outputs (name + channels in the head; NOT HERE columns amber). Cells: a compact level bar + dB ("0.0", "−6.0", "Off"); click to select, drag or type to change; a muted output's column shows its M chip in the head. Help text under it; footer as the tab.

### B.3 Chain with jack tiles and a split (M11Merge, M11Input, M11Sends)
- **IN tile** (JackTile, 64 × 84): label IN, jack icon, input name ("Guitar"), channel caption ("1 · Input 1"). Missing: amber border and name, NOT HERE in the inspector.
- **OUT tile** (104 × 84): label OUT, up to 3 send lines (name · level bar 26 px / "off" / M chip), "+ N more".
- **Split**: SPLIT tile (52 wide utility tile, icon split, "A + B" / "A only") and MERGE tile; between them two stacked rows, path A (y 14) over path B (y 110), connectors drawn as 2 px #5A5A60 lines. Path B off: its row at 40 % with an OFF chip. Right-click a block: "Move to path A / Move to path B" in its menu, in addition to today's items.
- **OUT selected → block stage "SENDS · CLEAN"**: SendFader strips (vertical): 92 wide, name 12.5 / 600, channels caption ("1/2 · stereo"), **On/Off** button (48 × 20, LED dot), fader track 196 high with the scale (+6, 0, −6, −12, −24, −40, −∞), meter beside it, value field below ("−6.0 dB" / "Off"). Chips under the strip: OUTPUT MUTED (#5A1A16 / #FFB4AD), NOT HERE (amber). Fader law: +6 → 0, 0 → 0.1, −6 → 0.25, −12 → 0.4, −24 → 0.6, −40 → 0.8, −∞ → 1 (piecewise linear between). The rows variant (M11SendsRows, 40 px rows with a horizontal fader) is kept as an idea, not to build.
- **MERGE selected**: the merge placeholder with Level A / Level B / Pan A / Pan B knobs; inspector shows path A and B blocks and whether the patch ends stereo.
- **IN selected**: input name popup (the computer's input names + any NOT HERE name), its channel, meter.

### B.4 Bottom row (M11BottomRow)
- Left pane: "Input" + the playing patch's input name and channel (amber with NOT HERE when missing).
- Centre pane: "OUTPUTS" + **Compact | Full** segmented (remembered per computer in `AppSettings`). OutputStrip each: name, channels, stereo meter, **M** (MuteButton), level value. Compact: strips share the width (up to 8 fit at 1440). Full: 170 px strips, horizontal scroll. Warning line on the right pane: "⚠ Output “Amp DI” is muted".

### B.5 Stage (M11Stage)
- New widget **Outputs** ("OUTPUTS" title; layout tag `outputs`): one row per output: name, meter, and a 52 px **Mute / MUTED** button (MUTED gradient when muted). Big touch targets; no confirmation pop-up. The big Mute in the toolbar stays where it is.
- Patch widget: under the patch name, the input (jack icon + "Guitar").

### B.6 Pre-show check, MIDI
- Pre-show lines from plan §3.8 with **Routing...** (opens Edit › Routing view).
- MIDI: action menu gets **Mute output ▸** (submenu of the computer's outputs). A binding to a name not here: the row's trigger text in amber "· no output “Monitor wedge” here", row tinted amber 8 %.

---

## C. New kit components (`Source/ui/kit`) and tokens
| Component | Use | Notes |
|---|---|---|
| **ProgressBar** | scan progress | 4 px, track `meterTrack` #0E0E10, fill `accent`; value 0–1; repaint on change only |
| **UtilityTile** (base of JackTile, PluginTile, Split/Merge tiles) | chain tiles that aren't gear | 84 high, radius 8, #2C2C30 → #1F1F22, border #3A3A3F, label 8.5 / 800 caps #85858C at 7,6, dot 6 px at right 7 (omitted when narrower than 60), name 10.5 / 600, caption 9 #85858C; selected = 3 px #4FA3FF outline 3 px out; amber state = border #FFB340, name #FFD27A |
| **FaultCard** | a block switched off / not loaded | over the dimmed placeholder; amber tones (`noticeAmberBg` #2A251C, border #3D2F12, text #FFD27A) |
| **ParameterList** | plugin window without an editor | ListBox rows 32, slider + value text |
| **Fader** (vertical SendFader / horizontal) | sends, output levels | law above; thumb as the volume slider; meter optional; double-click 0 dB; −∞ at the bottom = Off |
| **MuteButton** ("M") | per-output mute | 24 × 20; on = MUTED gradient #E8483E → #BF2C25, white |
| **Chip** | NOT HERE, OUTPUT MUTED, OFF, EXP n, VST3, counts | 16 high, radius 3, 9.5 / 700; variants mute, amber, exp, off, green |
| **OutputStrip** | bottom row, Routing outputs | compact / full (170) |
| **SendMatrix** | Routing › Sends | grid; heads 10.5 caps; cells 26 high |
| **SmallKnob** | output EQ | 36 px UI knob with a value arc (accent), label + value under |
| **EqCurve** | output EQ, thumbnails | response of the 5-band EQ, log frequency 20 Hz–20 kHz, ±12 dB, cached path rebuilt on change; later the graphic EQ (PROPOSALS) |
| **MeterBar** (horizontal/vertical, stereo) | outputs, inputs | existing MeterBar gains a stereo variant |
| **OutputsWidget** | Stage | rows of meter + 52 px Mute/MUTED |

New tokens (names to add to `EncoreTheme.h`): `chipMuteBg/Text`, `chipAmberBg/Text`, `chipExpBg/Text`, `chipOffBg/Text`, `chipGreenBg/Text`, `utilityTileTop/Bottom/Border`, `selectionOutline` #4FA3FF, `faultCardBg/Border/Text`, `progressH` 4, `utilityTileH` 84, `jackTileW` 64, `outTileW` 104, `splitTileW` 52, `sendFaderW` 92, `sendFaderTrackH` 196, `outputStripFullW` 170, `stageMuteW` 52, `smallKnob` 36, `pathRowGap` 96 (path A → path B row distance).
