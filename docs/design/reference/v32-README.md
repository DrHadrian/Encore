# v3.2 target pictures: amp builder inspector, amp faces, knobs (sections 1 and 2)

Made 19 Nov 2026 for `claude/PROMPT-V3.2-TARGETS.md`, from the board **BAmpBuilder** (canvas page "v3.2 — Working copy"; source `design/boards/BAmpBuilder.dc.html`). Rendered in Chrome from the board itself, so every token and word is the board's; nothing was redesigned. Sections 3–5 of the prompt (bottom row, the rest of v3.2, the font) are still to do.

**Format.** Every picture comes at 1x (`v32-<name>.png`) and 2x (`v32-<name>@2x.png`, device pixel ratio 2), with `v32-<name>.json` in the format of `docs/design/reference/geometry/edit.json`: `{"size":[w,h],"note":…,"elements":[…]}`, CSS px at 1x, **relative to the picture's top-left**. Fields as in edit.json (`t x y w h text font color ls bg bgImage border radius shadow opacity label role`), plus:
- `outline` and `outlineOffset`, `bgSize` and `bgPos` (with `bgImage`);
- `src` (for images), `filter`, `transform`;
- `checked` (aria-checked radio), `focusVisible` (the element with keyboard focus);
- `overflow` (text wider than its box).

Image URLs are reduced to the file name (`url(panel_tall_gold.webp)`): the layers are in `design/textures/AMP-BUILDER-LAYERS.md` and the sprites in `docs/design/assets/gear-art-later/`.

**Where the files are.** The project only holds text, so this README and the JSON files are in the project under `design/reference/v32/`. The two `v32-knob-scales-*.json` files are the exception: they're only in the zip, because they're large and the scale rules are written out below.

The PNGs come in `v32-targets-1-2.zip`, which has every file with the same names. Put them next to the JSON, and in the repo under `docs/design/reference/target/` + `geometry/`.

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
- **Row label when a group wraps** (Tolex and Panel wrap to 2 lines at this width): the label is **centred vertically on the whole group** (`align-items: center`): Tolex label y 225, group 207–257. Exceptions with the label at the **top**: Body (`align-items: start`, label `padding-top: 4px`) and Symbol (`padding-top: 6px`).

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
- **Which wins:** the app's 2-column choice (decided 19 Nov, "option buttons on one line") is better. Keep it; the board's 3 columns are too tight.
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
- **Body choice:** 2 columns in the app vs 3 on the board. The app's 19 Nov decision wins (see above).
- **Source Serif 4 (prompt §5):** not attached. This environment can't download it. Get `SourceSerif4-Bold.ttf` and `OFL.txt` from Adobe's source-serif repository (SIL Open Font License 1.1). None of these pictures use the serif. Super Crush 100 does, and is not rendered here.
- **Sections 3–5** of the prompt (`v32-edit`, `v32-edit-mixer` and the tab chevron; status bar, menu, settings, layout drag states, the amp builder screen, health window, Stage): next.
