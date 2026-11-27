# v3.2 target pictures (sections 1 and 2, and A2: the rest of v3.2)

Made 19 Nov 2026 for `claude/PROMPT-V3.2-TARGETS.md`, from the board **BAmpBuilder** (canvas page "v3.2 — Working copy"; source `design/boards/BAmpBuilder.dc.html`). Rendered in Chrome from the board itself, so every token and word is the board's; nothing was redesigned. **A2** (the rest of v3.2, `claude/PROMPT-DESIGN-TODO.md`) is further down: Edit with the bottom row and mixer view, status bar, Show menu, Settings, the layout editor's drag states, the amp builder, and Stage at the BStage sizes.

**Format.** Every picture comes at 1x (`v32-<name>.png`) and 2x (`v32-<name>@2x.png`, device pixel ratio 2), with `v32-<name>.json` in the format of `docs/design/reference/geometry/edit.json`: `{"size":[w,h],"note":…,"elements":[…]}`, CSS px at 1x, **relative to the picture's top-left**. Fields as in edit.json (`t x y w h text font color ls bg bgImage border radius shadow opacity label role`), plus:
- `outline` and `outlineOffset`, `bgSize` and `bgPos` (with `bgImage`);
- `src` (for images), `filter`, `transform`;
- `checked` (aria-checked radio), `focusVisible` (the element with keyboard focus);
- `overflow` (text wider than its box).

Image URLs are reduced to the file name (`url(panel_tall_gold.webp)`): the layers are in `design/textures/AMP-BUILDER-LAYERS.md` and the sprites in `docs/design/assets/gear-art-later/`.

**Where the files are.** The project only holds text, so this README and the section 1–2 JSON files are in the project under `design/reference/v32/`. The two `v32-knob-scales-*.json` files and **all A2 JSON files** are only in the zips (they're 6–55 KB each).

The PNGs come in `v32-targets-1-2.zip` (sections 1–2) and `v32-targets-a2.zip` (A2), with the same names. Put them next to the JSON, and in the repo under `docs/design/reference/target/` + `geometry/`.

The JSON also lists the parent containers around each picture (the page, the "Amp builder" section, "Amp face"), at negative or out-of-picture coordinates. Ignore those.

## Files

| File | Size (1x) | State |
|---|---|---|
| `v32-amp-inspector-gold` | 300 × 650 | (a) Encore Gold 30 (template British gold), nothing selected: the whole-amp inspector |
| `v32-amp-inspector-grille-head` | 300 × 650 | (b) the Grille head template, shown with its example amp 5150 III 50W, nothing selected |
| `v32-amp-inspector-knob` | 300 × 598 | (c) Encore Gold 30, knob k4 "TREBLE" (drives Treble) selected: Drives, Knob, Size, Label, Scale, Text, Position, Size |
| `v32-amp-inspector-section` | 300 × 370 | (d) JC-120, panel section s1 (black) selected: Material swatches |
| `v32-amp-inspector-focus` | 300 × 650 | extra: Encore Gold 30 with keyboard focus on the Tolex swatch "Cream" |
| `v32-face-gold` | 600 × 430 | (a) Encore Gold 30 face, transparent around the amp |
| `v32-face-jc120` | 600 × 430 | (b) JC-120 face (black panel) |
| `v32-face-5150` | 600 × 430 | (c) 5150 III 50W face (brass grille, chickenhead knobs) |
| `v32-knobs-gold` / `v32-knobs-black` | 588 × 786 | every knob style (rows) × every size S 26 / M 34 / L 48 / XL 64 (columns) on the gold / black panel, value 0.5, nothing else |
| `v32-knob-scales-gold` / `-black` | 588 × 478 | extra: the three scales (ticks, 1–10, 1–12) around a chickenhead in each size |

The inspector pictures are the inspector column (x 1131 at 1440 × 900), from its header to the last row plus the 10 px padding. The board has no "Look" heading: the column starts with the pane header ("Amp · Encore Gold 30", drawn in caps by the `.hd` style; JSON `text` is the source case) and its hint ("the whole amp", "click the amp body for the amp"). At 1440 × 900 the inspector doesn't scroll in any of these states.

The face pictures are the 600 × 430 amp box with everything around it cleared. The builder's dot grid isn't part of the face: in Edit the box sits on the block stage. The box itself has `filter: drop-shadow(0 24px 20px rgba(0,0,0,0.55))` (the JSON shows it on the 600 × 430 `div`). Most of that shadow falls outside the crop, so draw it in the app as the box's shadow, not inside the face.

The knob sheets aren't a board. They're built in a small page with the board's exact knob CSS: `knobImg()`, the knob filter, the pointer, the ticks and numbers from the board code. Each row is one panel strip (`panel_tall_gold.webp` / `panel_tall_black.webp`, 556 × 138). Knob centres are at x 86 / 214 / 342 / 470 and y 69 of each strip: 128 px pitch, so the XL knobs' boxes are 64 px apart.

## 1. Inspector: swatches, rows, selection

**Layout** (measured, 1x):
- Inspector column 300 wide, `border-left` 1 px #0A0A0B; header 24 (`.hd`); body padding 10 12, a column with gap 6.
- Rows: `.ab-row` = grid `86px minmax(0,1fr)`, gap 8, `align-items: center`, **min-height 26**. So the **label column is 86** (label at x 13, 11.5/400 #A3A3AA), and the swatches start at x 107.
- **Swatch** (`.ab-sw`):
  - 22 × 22, radius 5;
  - border 1 px #0A0A0B;
  - inner ring `inset 0 0 0 1px rgba(255,255,255,0.06)`;
  - flex-wrap row with **gap 6 across and 6 down** (pitch 28 both ways).
- **From one swatch group to the next** (Tolex → Piping → Panel → Grille): **8 px** from the last swatch's bottom to the next group's first swatch. That's the column gap 6 plus 2, because a one-line row is 26 high with its 22 px swatches centred.
- **Row label when a group wraps** (Tolex and Panel wrap to 2 lines at this width): the label is **centred vertically on the whole group** (`align-items: center`): Tolex label y 225, group 207–257. Exceptions with the label at the **top**: Body (`align-items: start`, label `padding-top: 4px`) and Symbol (`padding-top: 6px` on the icon grid; 4 px since A1 made it an icon + word list in 2 columns).

**Selected swatch** (`.ab-sw-on`):
- `outline: 2px solid #E8E8EB; outline-offset: 1px`.
- In Chrome the outline follows the border radius: inner edge 1 px outside the swatch, radius 6; outer edge 3 px outside, radius 8. The ring's outer bounds are 28 × 28 at (x−3, y−3).
- In JUCE: `drawRoundedRectangle (swatch.expanded (2.0f), 7.0f, 2.0f)`, i.e. the stroke centred 2 px out with radius 7.

**Keyboard focus** (global `button:focus-visible`):
- The same geometry in accent: `outline: 2px solid #4C8DF6; outline-offset: 1px` (see `v32-amp-inspector-focus`).
- On the selected swatch, focus wins (higher specificity), so the white ring turns blue while it has focus.

**Hover:** the board has **no hover look** on swatches (only `cursor: pointer`). Nothing to draw.

**What each swatch shows:**

| Group | Fill | Notes |
|---|---|---|
| Tolex | flat CSS colour | Tweed is stripes: `repeating-linear-gradient(135deg, #7A5228 0 1px, #D9B878 1px 3.4px)`. Black #1C1B1A, Orange #C8601E, White #E0DED6, Cream #D6C6A0, Brown #4E3220, Red #6E1C18, Blonde #C4A470. Order: Tweed, Black, Orange, White, Cream, Brown, Red, Blonde. |
| Piping | flat CSS colour | Gold #D6C080, White #E4E2DC, Black #1E1E20. None = `linear-gradient(135deg, transparent 44%, #FF8A80 44% 56%, transparent 56%), #2A2A2E` (a red slash). |
| Panel | image crop | `panel_tall_<material>.webp` (556 × 138 CSS), `background-size: cover`, `background-position: center`. Scaled to 22 high (88.6 wide), the centre 22 px shows. Order: Brushed gold, Brushed silver, Black, Plexi gold, Dark steel, White, Orange. |
| Grille | image crop | `grille_tall_<cloth>.webp` (556 × 214 CSS), cover, centre (scaled 57.2 × 22). Order: Oxblood weave, Silver-blue weave, Wheat weave, Salt & pepper, Black mesh, Perforated brass. Hidden for the Head body, which has no grille. |
| Section material (d) | image crop | `panel_tall_<mat>.webp`, cover, centre: Black, Gold, Silver, Orange, White. |
| Light colour, logo colour | flat colour | not in these states |

Under the image the button keeps the browser's default grey (#EFEFEF in the JSON `bg`). It never shows, because the crops are opaque in the centre. In JUCE, fill with the image only.

**Body choice** (5 options):
- On the board it's a grid of **3 equal columns**, gap 4, buttons 24 high, 11/500, `padding: 0 4px`, `white-space: nowrap`: Tall · Thin · Top panel / Head · Grille head.
- At the inspector width each column is **57.7 px**.
- "Grille head" is 56.6 px wide in Inter 500 11 px, but a button only has 47.7 px of content box (57.7 − 2 border − 8 padding). It **runs past the padding to the border** (0.4 px over on each side) and only just fits the button.
- **Which wins:** the app's 2-column choice (decided 19 Nov, "option buttons on one line") is better. Keep it; the board's 3 columns are too tight. *(A1, 19 Nov: the board now has 2 columns too. These pictures were rendered before that.)*
- The Template choice is 2 columns on the board too.

**Knob inspector (c), for reference:**
- Drives: 3 columns, buttons 22 high.
- Knob: 4 columns of 42.3 × 34 picture buttons, so the fifth (Silver) wraps.
- Size, Label and Scale: segmented controls.
- Selected buttons use the pressed gradient #252528 → #2E2E31 with white text. No accent bar (Adrian's rule).

## 2. Knobs on a face and their shadow

**The shadow** (one rule for every knob style, in the board's `knob` part):
- CSS `filter: drop-shadow(0 Ypx Bpx rgba(0,0,0,0.55))` on the knob's round element (`border-radius: 50%`).
- `Y = max(2, s/18)`, `B = max(1, s/30)` (B is the CSS blur radius; Gaussian sigma ≈ B/2).
- No x offset, no spread.

| Size | s | Y (offset down) | B (blur) |
|---|---|---|---|
| S | 26 | 2 | 1 |
| M | 34 | 2 | 1.13 |
| L | 48 | 2.67 | 1.6 |
| XL | 64 | 3.56 | 2.13 |

- **Shape:**
  - Drop-shadow follows the alpha of what's drawn. For knurled, cream and silver (round sprites, no baked shadow) and the CSS "black, white line" knob, that's a circle.
  - For the chickenhead it follows the pointer outline, and it adds to the sprite's own baked shadow (next point).
- **Chickenhead's baked shadow** (`render_chicken.py`): every frame of `chicken_strip.png` has a soft contact shadow baked in. It's offset 5 % of the frame down and 3 % right, Gaussian σ 4.5 % of the frame, alpha up to 0.65. So on a chickenhead you see both: the baked one and the CSS drop-shadow on top.
- **Doesn't change with panel colour.** It changes only with size, by the formula above.
- The pointer line on the non-chickenhead knobs has its own `box-shadow: 0 0 1px rgba(0,0,0,0.6)`. It's 2 px wide, #F2F2F4, radius 1, length `round(s × 0.36)`, from the centre outwards, rotated −135° + 270° × value.
- Chickenhead frame = `round(value × 40)` of the 41-frame strip, `background-size: s px auto`.

**Ticks** (scale "Ticks"):
- 11 ticks from −135° to +135°, 27° apart; ticks 0, 5 and 10 are big.
- Colour: the print colour where the knob sits (gold panel #4a3a1c, black #E8E8EB; on a section see AMP-BUILDER.md).
- Rounded 1 px.
- **Distance from the knob:** the inner end of each tick is **1.5 px** from the knob's edge for S and M (s < 40), **2 px** for L and XL.

| | S, M (s < 40) | L, XL |
|---|---|---|
| big tick, length × width | 3 × 1.2 | 5 × 1.6 |
| small tick, length × width | 2 × 0.9 | 3.5 × 1 |
| gap from knob edge | 1.5 | 2 |

**Numbers** (scale "1–10" / "1–12"):
- Inter 700, tabular, line-height 1, print colour.
- Font size **6 px** (s < 40) or **7.5 px**.
- Each number is centred at radius `s/2 + 2 + 0.6 × font size` from the knob centre: S 18.6, M 22.6, L 30.5, XL 38.5.
- Angles from −135° to +135° in equal steps (9 steps for 1–10, 11 for 1–12). Upright, never rotated.

**Labels** on the faces (in the face JSON):
- 9.5/800, letter-spacing 0.1em for knobs of 32 px and up; 6.5/800, 0.02em below that.
- Distance from the knob: 3 px; 9 px with ticks, 11 with numbers (labels above). Below: 4, 8, 6.

## Open (what the boards leave open)
- **Swatch hover:** the board has none. If the app wants one, it's new design: say so and I'll add it.
- **Body choice:** settled: 2 columns, now on the board too (A1).
- **Source Serif 4 (prompt §5):** not needed: the Serif text style was dropped on 20 Nov (Adrian). Face text is Inter caps only; Super Crush 100's model name is caps now.
- **A2 and A3:** done, see below. The milestone 12 sandbox screens (A4) are in `design/reference/m12/README.md`.

---

# A2: the rest of v3.2 (19 Nov)

Rendered from the canvas page "v3.2 — Working copy" at 1440 × 900 (1x and @2x), with the same JSON format as above. JSON `x`/`y` are the box as drawn: for the mixer tab that already includes its `translateX(-50%)` (the `transform` field is only for information).

**New and changed boards** (all on the canvas, page v3.2):
- **BEditV32** (new): BEdit with the v3.2 bottom row from BBottomRow / BBottomRowLarge. Click the tab on the Outputs pane to switch between collapsed and the mixer view.
- **BShowMenu** (new): BEditV32 with the Show menu open.
- **BSettingsAudio** (new): the v3 Audio tab brought to v3.2.
- **BLayout**: drops and moves now behave as decided (nearest free space, amber when none); the "At rest" option for Next button and Panic button.
- **BStage**: Next button and Panic button looks are a per-widget option (default Full colour).

## Files

| File | Size (1x) | Board | State |
|---|---|---|---|
| `v32-edit` | 1440 × 900 | BEditV32 | Edit, bottom row collapsed (104 px, the default), 3 outputs, "Amp DI" muted so the health status says "1 thing to check" |
| `v32-edit-mixer` | 1440 × 900 | BEditV32 | the mixer view open (196 px): the patch area gives up 92 px, the amp and cab scale to fit (see below) |
| `v32-status-bar` | 1440 × 44 | BEditV32 | the Edit toolbar with the status bar (`.sbar`, §5 of V3.2-CHANGES) |
| `v32-menu` | 1440 × 900 | BShowMenu | the Show menu open, Stage settings (this show) highlighted with its submenu |
| `v32-settings` | 1440 × 900 | BSettingsAudio | Settings… on the Audio tab |
| `v32-layout-drop` | 1440 × 900 | BLayout | "Tuner" dragged from the widget list onto free cells: drop box "Drop: Tuner", 4 × 2, snapped |
| `v32-layout-move` | 1440 × 900 | BLayout | "Clock" being moved: the original at 35 %, drop box "Move: Clock" |
| `v32-layout-refused` | 1440 × 900 | BLayout | the default layout is full, so a 4 × 2 drop has no free space anywhere: the box turns amber, nothing moves |
| `v32-layout-at-rest` | 1440 × 900 | BLayout | extra: Next button selected, the inspector's new "At rest" choice set to Outline |
| `v32-amp-builder` | 1440 × 900 | BAmpBuilder | the builder (F8), Encore Gold 30, the Treble knob selected and being dragged: smart guide on the knob row, equal-spacing marks "10", drop box |
| `v32-stage` | 1440 × 900 | BStage | Stage at the BStage sizes, Next and Panic at Full colour (default) |
| `v32-stage-outline` | 1440 × 900 | BStage | extra: the same with Next button and Panic button set to Outline |

In `v32-layout-drop` and `-move` the default layout's "Level meters" widget is removed, to leave free cells. The layout menu, which is drawn open on BLayout, is closed in these pictures.

## The bottom row and the mixer tab

- Collapsed: grid `250 | 1fr | 116`, 104 high. Mixer view: grid `auto | 1fr | 116`, 196 high (the Input strip comes out 94 wide). Both sit 8 px from the window's left, right and bottom edges.
- **The tab** (PaneTab): 44 × 12 at `top: -7`, background #1D1D20 (the pane), border 1 px #0A0A0B without the bottom edge, radius 6 6 0 0, **centred on the Outputs pane**: collapsed x 765–809, open x 687–731 (pane centres 787 and 709).
- **Chevron:** 10 × 10, viewBox 24, stroke 3, round caps and joins, #A3A3AA (hover #FFFFFF). Collapsed, it points **up**, path `M6 15l6-6 6 6` ("Open the mixer view…"). Open, it points **down**, path `M6 9l6 6 6-6` ("Close the mixer view").
- **Board conflict:** BBottomRowLarge draws the open tab off-centre (left 253 / 497). V3.2-CHANGES §3.4 says centred on the pane, so **the spec wins**; BEditV32 and these pictures centre it.
- **Where the tab shows** (decided 19 Nov): wherever the bottom row is, so Edit and Routing (F7). Never on Stage or in the amp builder (F8).
- **System health status:** centred vertically in the collapsed row, at the bottom (margin 4) in the mixer view.

## The amp when the stage gets shorter (decided 19 Nov)

With the mixer view open at 1440 × 900 the block stage is shorter than the 600 × 430 amp box. **The amp and the cab scale down evenly to fit, stay centred, and the knobs keep working; back to 1× when the stage is tall enough.** Scale = `min(1, stage content height / 430)`; here **0.95** (the amp box is drawn 570 × 408.5). Never above 1×.

## Show menu

- Words from SCREENS §10 with "…": New show… · Open show… | Save now (Ctrl+S) · Save a copy to… (USB stick, backup) | Show folder in Explorer · Kind of show ▸ | Pre-show check… (F9) · Open log folder | Stage settings (this show) ▸ | Open backups folder. Submenu: ✓ Tuner mutes the output · Concert pitch ▸ · Panic ▸.
- **Check against the code:** if the app's Show menu gained items after milestone 9 (M10–M11), the code's list wins; tell me and I'll redraw.
- Style is §6 of V3.2-CHANGES with the 19 Nov separator #3A3A3F. Menu 290 wide at (126, 38), under the Show button. Submenu 220 wide, overlapping the menu by 4 px, its first item level with "Stage settings (this show)". In a menu with check marks, items have 20 px on the left (6 padding + 14 for the ✓). Shortcut and ▸ in #9A9AA1, white on the highlighted item.

## Settings › Audio

- The v3 Audio tab (V3Audio) with the v3.1 tokens, slashed zero and "…", in the v3.2 Settings frame (as BMidi): **Done only, no Cancel** (audit F18, as on BMidi), and the keyboard line in the footer.
- **Check against the code:** the words are the v3 board's. Two may have changed with milestone 11: "Active channels" and "Which input is the guitar is chosen under **Guitar input** in the main window." (the input is now per patch, picked in the bottom row). The code's words win.

## Layout editor: drops (decided 19 Nov)

- A drop or move onto cells another widget uses goes to the **nearest free space of the same size** (smallest distance in cells). The drop box already shows that place while dragging.
- If there's **no free space** anywhere, the drop is **refused**: the box turns amber (border 2 px dashed #FFB340, fill rgba(255,179,64,0.14), text #FFD27A) and nothing changes on release. The label stays "Drop: <widget>" / "Move: <widget>". If the build already has its own refused words, they win.
- A click in the widget list adds the widget at the **first free place** (top-left first); with none free, nothing is added.
- New widgets are 4 × 2.

## Stage (BStage sizes, taken in 19 Nov)

Taken in with the sizes (decided 19 Nov):
- **S2** tap again: a tap on another song arms it (amber outline, "Tap again to go to 6", 2 s); a second tap goes.
- **S3** hold to open Tuner (the button says "hold").
- **S7** lit stomps: on = red outline and fill with a big "ON", off = grey with "OFF".

**S6 is an option per widget**, not a fixed look (decided 19 Nov). "At rest" on **Next button** and **Panic button**, each set separately in the layout editor's inspector, and in the widget's right-click menu ("At rest ▸ Full colour / Outline"):
- **Full colour** (the default, as today): Next = primary gradient #3570DA → #2860C6, border #0B3E86, white text. Panic = #A62B24 → #761A15, border #C2392F, white text, sub line #FFD3CE.
- **Outline:** the widget background (#1C1E23), 2 px border in the colour (Next #4C8DF6, Panic #C2392F), text white / #FF8A80, Panic sub line #A3A3AA.
- Pressed, it always lights fully.
- Inspector words (approved by Adrian, 19 Nov): label "At rest", choices "Full colour" | "Outline"; tooltips "Filled with its colour all the time" / "Dark with a coloured outline until it's pressed, so the cue and any warning stand out in a dark room"; caption "Outline keeps the stage dark until you press it. Pressed, it always lights fully."
- Stored per widget in the layout (with the show).

---

# A3: System health window (19 Nov)

Board **BHealth** (canvas page v3.2; prop `state` = check | healthy). Adrian: "fine for now"; a bigger rework may come later. Files in `v32-targets-a3.zip`:

| File | State |
|---|---|
| `v32-health` | one thing to check (Output "Amp DI" is muted) |
| `v32-health-healthy` | healthy: no "To check" group |

- Opens from the bottom row's health status, **Edit only** (never on Stage), as `HealthDetailsDialog` does today. DialogFrame **640 × 492**, title "System health", ✕ "Close system health". Footer: **Reset counters** (tooltip "Start the overrun, dropout and clip counts again from zero") and **Done** (primary). Replaces today's 460 × 250 list of lines (`healthDialogW/H`).
- **Summary** (top): a 34 px round badge (amber #3D2F12 with a 3 px "!" in #FFB340, or green #1E3A24 with a ✓ in #30C25A), title 16/700 ("1 thing to check" / "N things to check" in #FFB340, or "Healthy" in #E8E8EB), sub line 12 #A9A9AF "CPU 9% · 5.3 ms round trip". Right: a 6 px green dot + "Live" 11 #9A9AA1 (tooltip "Updates while this window is open").
- **To check** (only when there's something): one 26 px row per item of `healthProblems`, warning icon 13 px + text 12.5 #FFB340, one line with ellipsis, full text in the tooltip.
- Two columns, gap 16:
  - **Audio:** CPU, Overruns, Driver dropouts, Bad audio caught (`getBadOutputCount() + getBadInputCount()`), Limiter ("idle" or "−3.2 dB"), Clips, Memory ("101 MB of 15 GB"); values right-aligned, tabular + slashed zero. Ringing out shows as an extra row only while something rings. Under it 11 #9A9AA1: "Counts since the last reset. Reset, then play the show's heaviest patches."
  - **Device:** Interface ("Focusrite USB ASIO · ASIO", the device line), Rate, Buffer ("64 samples (1.3 ms)"), Latency ("in 2.6 + out 2.7 = 5.3 ms"; "+ chain …" when there is chain latency). No device: Interface "No audio device open", the rest "-". Lost: "Reconnecting to <device>…" in amber.
  - **Computer:** Mains power / "On battery: plug in for shows", High priority, Power throttling off, Pro Audio audio thread: a green ✓ each when fine; when one isn't, an amber warning icon and the code's words ("normal priority (couldn't raise)", "power throttling ON (couldn't disable)", "audio thread not yet registered").
- Groups use `.gt` titles and `.grp` boxes (#29292D), as in Settings.
