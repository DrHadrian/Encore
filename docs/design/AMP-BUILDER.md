# Amp builder (new feature; designed 16–18 Nov 2026, scheduled 18 Nov: build after the v3.2 steps)

> In this repo: the board source is `docs/design/boards/BAmpBuilder.dc.html`, the layers `docs/design/textures/AMP-BUILDER-LAYERS.md` and `render_amp_parts.py`; the layers ship in `Resources/ui/amp_parts/`. "As built" at the end says how the app does it.

Board: "Amp builder" (BAmpBuilder) on the canvas page **v3.2 — Working copy**. Its full source, with every number, is in the project as `design/boards/BAmpBuilder.dc.html` (a drawing to read values from, never code to port). Layers: `design/textures/AMP-BUILDER-LAYERS.md`.

## What it is
A full-area view in Edit (like Routing; key F8, to confirm) where you build an amp's face and save it, with its NAM capture, as an amp preset. In Edit the amp block shows that face in the fixed 600 × 430 amp box, so no layout moves.

## Decided with Adrian
- **A preset = face + one NAM capture + default settings.** Several captures per amp (e.g. a stepped Gain knob) is for later.
- **Stored in the show, with a copy in this computer's amp library.**
- **Free placement on a snap grid, starting from templates** (British gold, Black panel, Plexi, Cream, Framed head, Grille head, Tweed top, Jazz clean).
- **The logo is always the Encore logo** (colour: cream, white, gold, black; size can change). No brand logos or image uploads.
- **Only add parts that are useful for many amps.**

## Rules
- **Nothing pretends.** A knob either drives Input, Bass, Mid, Treble or Output, or is **Fixed**: it shows the setting the capture was made at, can't be turned, and its tooltip says "Fixed at 6 in this capture". A knob that is neither can't be saved. A switch (bat toggle or push button) drives Level match or is Fixed. A light (pilot jewel or LED) lights while the block is on, or is always off (it was off when the capture was made).
- **Faces are composed to one cached image** (when an amp is saved or a show loads), never drawn part by part on stage.
- **Fonts:** Inter only (caps print). No serif and no other fonts (the Serif style was dropped on 20 Nov).

## Placement
- Parts snap to a fine 2 px grid for small details.
- **Smart guides while dragging:** a part's centre snaps to another part's centre when it's within 6 px, across (same height) and down (same column). A magenta line (#FF4FD8) shows the match.
- **Equal spacing:** in a row of lined-up parts, the dragged part snaps to the same gap as the two parts next to it, and both gaps are marked with their size in px.
- **Several parts:** shift- (or ctrl-) click to select several. The inspector then shows **Arrange**: Align tops, Align middles (on the first part picked), Align bottoms, Space evenly (the outer two stay put), Same size (as the first part picked).

## The parts
- **Controls:**
  - Knobs: chickenhead, black knurled, black with a white line, cream dome, silver. Sizes S 26 / M 34 / L 48 / XL 64; label above or below; **scale: ticks, numbers 1–10, numbers 1–12, or none** (numbers are printed around the knob in the panel's print colour, upright, 6 px on S/M knobs, 7.5 px on L/XL). Each drives Input, Bass, Mid, Treble or Output, or is Fixed.
  - Switch, in two styles: **bat toggle** or **push button** (18 × 18, chrome bezel, black cap; pressed in sits lower and darker). Drives Level match, or is **Fixed** (bat: up / down; push button: out / in), showing how it was set when the capture was made; it can't be switched. Labels above and below (e.g. OFF / ON, CLEAN / DIRTY).
- **Lights:** in two styles, **pilot jewel** or **LED** (a small flat LED in a chrome bezel, e.g. channel lights); red, amber, green or blue; **lights while the amp block is on**, or **always off** (for a channel light that was off in the capture).
- **Panel:**
  - Panel section: black, gold, silver, orange or white blocks to split a panel into channels. Print, ticks and symbols on a section switch to a readable colour automatically (light on black, dark on the others).
  - Frame line: a thin printed rectangle around a group or the whole panel.
  - Section title (caps with rules), label text (Inter caps; no style choice since 20 Nov), name plate (gold, silver, black, or **black with a gold rim**; shape **rectangle or oval**; an oval plate can carry the Encore logo as a badge).
  - **Symbol:** printed control symbols instead of or next to words: Volume, Treble, Bass, Mid / shape, Gain, Reverb, Power, Input, Clean, Dirty. In the inspector: icon + word, 2 columns (decided 19 Nov).
  - Badge: a small framed plate with the Encore clef.
  - Panel handle: a chrome grab handle at the side of a panel (heads and racks).
  - Input jack (decor), screw.
- **Front:** Encore logo (cream, white, gold, black; any size), trim strip (silver, gold, black).

## The amp
- **Body / panel layout:** Tall (combo, 138 px panel), Thin (combo, 72 px panel, big grille), **Head** (a short box with a deep framed panel, no grille, rubber feet), **Grille head** (a head with a grille above a bottom control panel, rubber feet) or **Top** (a combo with its controls on a panel on the top surface, seen from slightly above; the front is grille with a wide tolex border and room for a logo; for Blues Junior, Hot Rod, tweed and AC-style amps). All fit the same 600 × 430 box in Edit.
- Tolex: black, cream, brown, red, blonde, orange, white, tweed. Piping: gold, white, black or none.
- Panel: brushed gold, brushed silver, black, plexi gold, dark steel, white, orange.
- Grille (combos, top, grille head): oxblood, silver-blue, wheat, salt & pepper, black mesh, perforated brass.
- Corners: chrome, black or none. Studs: on or off.

## Example amps on the board
- **Encore Gold 30**: the Edit view's amp, rebuilt in the builder (tall, gold panel, oxblood grille).
- **JC-120**: a Jazz-Chorus-style 2-channel amp (thin black panel in four sections, a gold effects section, small black knobs, silver trim strips, black corners, studs). Channel 2's Volume, Treble, Middle and Bass and the Output knob are real; channel 1, Distortion, Reverb, Speed and Depth are Fixed.
- **Super Crush 100**: an Orange-style head (template "Framed head"): orange tolex, no piping, white panel in a deep frame, chrome panel handles, frame line, black caps model name "SUPER CRUSH 100" (was serif until 20 Nov), badge, two black sections and an orange one, black knobs with white lines and tick scales, printed symbols instead of words, two Fixed toggles (Power: OFF/ON, Channel: CLEAN/DIRTY), an amber pilot light, captions, black corners, rubber feet. Real: Master volume → Output, Dirty Gain → Input, Treble, Shape → Mid, Bass. Fixed: Dirty volume, Reverb, the clean channel.
- **Blues Junior**: a top-panel tweed combo (template "Tweed top"): tweed, no piping, brushed silver panel on the top, oxblood grille, no corners, black chickenhead knobs (M) with 1–12 number scales, a FAT push button (Fixed out), a power bat toggle (Fixed up), red pilot light, input jack, INPUT / FAT / POWER captions, a black name plate carrying the white Encore logo above the grille, four baffle screws. Real: Volume → Input, Treble, Bass, Middle → Mid, Master → Output. Fixed: Reverb, FAT, power.
- **5150 III 50W**: a three-channel high-gain grille head (template "Grille head"): black tolex, no piping, perforated brass grille, brushed gold bottom panel, black corners, rubber feet; an oval black plate with a gold rim carrying the gold Encore logo, a rimmed model plate "5150 III 50W"; 11 small chickenhead knobs with tick scales, two SELECT push buttons with channel LEDs (green always off, red lit), section titles ONE / TWO and THREE, INSERT jack, big red pilot jewel. Real: channel three's Gain → Input, Low → Bass, Mid, High → Treble, Volume → Output. Fixed: channel one/two, Presence, the select buttons. The real amp's striped grille overlay is left out: like a brand logo it belongs to the maker, so no part copies it.

## Building it (for the software chat)
How the design maps onto the app. Where the code suggests a better way, keep the **musts** and write the change up.

**Musts**
- Nothing pretends: a control on a face either drives a real parameter or is drawn as Fixed (knobs, switches) / always off (lights). A face with a knob that is neither can't be saved (Save amp disabled, the knob tagged NOT SET).
- The amp box stays 600 × 430 in Edit (the GearView contract); no layout moves.
- Faces are composed to one cached image when an amp is loaded, saved or edited. Nothing is composed or decoded while a patch plays, never on the audio thread, never in `paint()`.
- The logo is the Encore wordmark / clef (`Resources/ui/logo`), never a typed word and never a brand logo.
- Native JUCE only (`Graphics`, `Component`, `DragAndDropContainer`), tokens and kit components, no animation.

**1. Assets**
- Render the layers: `python3 -I design/textures/render_amp_parts.py <dir>` (numpy, scipy, Pillow; seeded, so the result matches the board). 227 WebP files, about 10 MB, 2× resolution (1200 × 860 for a body).
- The knob, switch and light sprites already in the repo kit (`docs/design/assets/gear-art-later/`, from TEXTURES.md): `chicken_strip.png` (chickenhead, 41 frames 0–40 for 0–1, one frame per knob width), `knob_black.png` (knurled), `knob_cream.png`, `knob_silver.png` (these three have no pointer: draw a pointer line on top), `toggle_lg_on.png` / `toggle_lg_off.png` (bat toggle up / down), `jewel_on.png` (pilot jewel; other colours are hue-shifted copies made at load).
- The "Black, white line" knob, the push button, the LED, the jack, the screw, the plates, symbols, ticks and numbers are drawn with `Graphics` (values in the board source).
- JUCE has no WebP decoder: add `libwebp` (vcpkg) and a small loader to `juce::Image` (premultiplied ARGB), or convert to PNG if the size stays reasonable; your call, note it in the PR. Load lazily on the message thread and cache by name.

**2. Data**
- **AmpPreset**: `id` (UUID), `name`, `version`, `look { body: tall | thin | head | headg | top, tolex, piping, panel, grille, corners: chrome | black | none, studs }`, `parts[]`, `capture` (the NAM file, as the amp block stores it today), the starting value of each driving knob/switch.
- **Part**: `id`, `type`, `x`, `y` (face pixels, 2 px grid), `w`, `h`, plus per type:
  - `knob`: `style` chicken | black | plain | cream | silver; `size` S 26 | M 34 | L 48 | XL 64; `drives` input | bass | mid | treble | output | fixed | (unset); `value` 0–1; `label`, `labelPos` above | below; `scale` ticks | n10 | n12 | none.
  - `toggle`: `switch` bat | push; `drives` levelMatch | fixed; `value` (1 = up / out); `top`, `label`.
  - `jewel` (light): `style` jewel | led; `colour` red | amber | green | blue; `lights` whileOn | off.
  - `section` (`mat` black | gold | silver | orange | white), `outline`, `title` (`text`), `text` (`text`; Inter caps. A `font: serif` from an older file loads as caps and is kept when saving), `symbol` (`icon`: volume, treble, bass, mid, gain, reverb, power, input, clean, dirty), `plate` (`text`, `mat` gold | silver | black | blackgold, `shape` rect | oval), `badge`, `handle`, `jack`, `screw`, `logo` (`colour` cream | white | gold | black, `size`), `trim` (`mat` black | gold | silver).
- Stored **in the show** (an amps list) and as a **copy in the computer's amp library** (one file per amp in the app-data folder). Keep unknown fields when saving (newer files stay readable). An amp block refers to its preset by `id`; each patch keeps its own knob values, as amp blocks do today (the face's values are only the starting settings).
- Parameter mapping on `NamAmpBlock`: input → `input`, bass → `bass`, mid → `mid`, treble → `treble`, output → `output`, Level match → `normalise`. Fixed controls drive nothing.

**3. Face rendering (kit: `AmpFace`)**
- Order: body, panel, grille, sections (the panel image of the section's material, clipped to the section), feet (head bodies), corners, studs, then the parts in list order.
- Print colour: the panel's print colour (table below); on a section, light #E8E8EB on black, #141416 on orange / white, #2a2112 on gold / silver. Ticks, numbers, symbols, labels and frame lines use it.
- In the builder every part is drawn live while editing (fine: it's Edit, not the show). On save (and when a show loads), everything except the **driving** knobs and switches and the **lights that follow the block's on state** is composed to one 2× image; those are drawn on top of it (a light just swaps between its lit and unlit look).

| Panel | Print | Rule / minor ticks |
|---|---|---|
| white, orange | #141416 | #3A3A3E / #3A2410 |
| gold | #4a3a1c | #6a5630 |
| plexi | #2a2112 | #5a4628 |
| silver | #2A2A2E | #55555B |
| black | #E8E8EB | #6A6A70 |
| dark | #E8E8EB | #8A8A90 |

**4. Edit integration (kit: `AmpFaceView`, a `GearView`)**
- When an amp block uses an amp preset, `BlockPanel` shows `AmpFaceView` in place of the amp placeholder: it draws the cached face image and puts the driving knobs and switches at their saved positions, in their style and size, as real controls bound to the block's parameters (same undo, MIDI-learn and tooltips as today's amp knobs).
- Fixed knobs/switches have a hover area only: tooltip "Fixed at 6 in this capture" (value × 10, one decimal, none when whole: "6", "6.5") / "Fixed up (out, down, pressed in) in this capture". Lights set to "lights while on" follow the block's on state; "always off" stays dark.
- Chain tiles and stage pedals keep today's amp tile (no face there).

**5. The builder view**
- A full-area view in Edit like Routing; **F8** opens it and returns to the patches (proposed; Routing has F7). Layout, sizes and words exactly as the board: sub-toolbar, Parts (220) | Face (600 × 430 at 1:1, dot grid) | Inspector (300).
- Parts list: drag onto the face (a drop box shows where). Click or Enter on a list item adds it at the face's centre (added for keyboard use; the board only shows dragging). Every part: drag to move (2 px grid), smart guides (centre match within 6 px, magenta #FF4FD8), equal-spacing snap, shift/ctrl-click multi-select, Arrange (Align tops / middles / bottoms, Space evenly, Same size).
- Keyboard (floor gate): Tab / Shift+Tab move through the parts, arrows nudge 2 px (Shift+arrows 10 px), Delete removes, Ctrl+D duplicates, Ctrl+Z / Ctrl+Y undo / redo, Esc clears the selection.
- Templates: British gold, Black panel, Plexi, Cream, Framed head, Grille head, Tweed top, Jazz clean. "New amp from a template…" in the amp menu; picking a template in the inspector asks nothing and replaces look and parts (it's undoable).
- Sound: NAM capture name, its format line ("48.0 kHz · standard · ~12% CPU" from the model), **Load capture…** (the existing NAM file chooser and loader, off the audio thread).
- Save amp: writes to the show and updates the library copy; disabled while any knob is NOT SET (tooltip says why).
- Snapshot tests: the builder with each of the five example amps, and the Edit amp panel showing a saved face.

**6. Example amps to ship as templates** (their parts lists are in the board source): Encore Gold 30 (British gold), JC-120 (Jazz clean), Super Crush 100 (Framed head), Blues Junior (Tweed top), 5150 III 50W (Grille head). Names of real amps appear only as the user's amp names and model-name text, never as logos.

## Open
- Rack body and wider cabinets (head and top panel are done).
- Confirm F8 for the builder (proposed).
- Several captures per amp.

## As built (v3.2, 18–19 Nov 2026)
- **Assets:** the 227 WebP layers are built into the app (`Resources/ui/amp_parts/`, binary data `EncoreAmpArt`) with the 8 sprites (`Resources/ui/amp_sprites/`). JUCE 9 decodes WebP itself (`JUCE_USE_WEBP`, on by default), so neither libwebp from vcpkg nor a PNG conversion is needed: the WebP files stay at ~10 MB where PNGs of the same noisy textures would be many times that. `kit::ampArt` loads them lazily on the message thread and caches them by name; the jewel's amber, green and blue (and its unlit look) are the board's CSS filters applied once at load.
- **Data:** `model/AmpPreset` (look, parts, capture; unknown fields kept), stored in the show (`"amps"`, schema 4; a show without amps is still saved as 3 or lower) and in this computer's library (`session/AmpLibrary`: `<app data>/Amps/<id>.encore-amp`). An amp block refers to its amp by `ampPreset` (id); `ShowSession::setBlockAmp` applies the amp's starting settings to the block, after which each patch keeps its own values.
- **Templates and examples:** `Resources/amps/amp-templates.json`, read from the board's source (the eight templates' looks and parts, the five example amps); `kit::ampTemplates`. No capture ships with them.
- **Face:** `kit::AmpFace` composes a face to one 2× image, cached by content (24 kept); `Layer::staticOnly` leaves the driving knobs and switches and the following lights out. Print colours, materials and the builder's marks are tokens in `EncoreTheme.h` (`theme::amp`). All face text is Inter ExtraBold caps; the serif (DejaVu Serif Bold) and `EncoreFonts::serifTypeface()` were removed on 20 Nov with the Serif style.
- **Edit:** every amp block shows a face (`kit::AmpFaceView` in `BlockPanel`): its own amp, or **Encore Gold 30** (every control on it drives a parameter). The placeholder is gone for amps; cabs, pedals and tiles keep theirs. A light that follows the block turns it on and off when clicked (the power LED's job; a face without such a light gets the LED at the top right of its panel).
- **Builder:** `ui/AmpBuilderView` (F8 in Edit, over the whole area under the toolbar; Back to patches or F8 again). It builds the selected amp block's amp (or Encore Gold 30 when it has none); **Save amp** writes the show and the library and gives that block the amp. **Load capture…** uses the NAM chooser and loads the file into that block. The amp menu offers the show's amps, *New amp from a template…* and *Add from the library…* (this computer's amps, then the examples not in either).
- **Decided 19 Nov:** pedals show on faces too: a face knob a pedal moves shows the "EXP 1" chip (as in the parameter list) on the side away from its printed label. The Body choice is 2 columns, not the board's 3, so "Grille head" fits whole at the inspector's width; option buttons never wrap (one line, squeezed a little, then cut).
- **Matched to the v3.2 targets (19 Nov):** `docs/design/reference/target/v32-amp-inspector-*`, `v32-face-*`, `v32-knobs-*` and `v32-knob-scales-*`, with their measurements in `geometry/` and the rules in `reference/v32-README.md`. The tests render the same pictures (`AmpBuilderTests`, "v3.2 targets"), so `ui-compare.py` checks them.
  - **Swatches:** 22 × 22, radius 5, a 6 px gap across and down, 8 px between groups. Labels are centred on a wrapped group (Body and Symbol sit at the top).
  - **Swatch fills:** Tolex and Piping are flat colours (Tweed is stripes, None a red slash). Panel, Grille and section crops are CSS cover/centre, averaged once per size (`kit::ampArt::cover`).
  - **Selected / focused swatch:** the selected one has a 2 px #E8E8EB ring 1 px outside; keyboard focus draws the same ring in the accent, and focus wins. There's no hover look.
  - **Knobs:** each is clipped to its circle (as the board's `border-radius: 50%`). Its shadow is the board's drop-shadow of what's drawn: black 55 %, max(2, s/18) down, blur max(1, s/30). It's made once per look and size and kept. This removes the dark halo the chickenhead's baked shadow made outside its circle.
  - **Column headers and Sound:** the builder's column headers are 25 px (the board's 24 + its line). The Sound section follows the board: a full-width line, Load capture… across the value column, and notes at line height 1.5 / 1.45.
  - **Knob choice:** the board's order (Chickenhead, Black knurled, Black white line, Cream dome, Silver), pictures only.
  - **Position / Size:** they read "x 278" and similar. A knob shows its w and h read-only.

