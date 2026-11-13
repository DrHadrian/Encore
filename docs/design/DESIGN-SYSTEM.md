# Encore design system (v3)

The look of every Encore screen, as rules a C++/JUCE implementation can follow exactly. Values are logical pixels at 100 % scale, measured on the 1440 × 900 reference boards in `reference/`. Every value here also exists as a constant in `theme/EncoreTheme.h`; code uses the constants, never literals.

**Where the truth is, in order:**
1. This file and `theme/EncoreTheme.h` (tokens and component rules).
2. `reference/target/*.png` — what the finished app must look like (gear shown as placeholders).
3. `reference/geometry/*.json` — the position, size, colour and font of every visible element on each target board, for exact measurements.
4. `reference/boards/*.dc.html` — the design sources (HTML used only as a drawing; read the inline styles for any value not listed elsewhere).
5. `reference/with-gear-art/*.png` — the same boards with the rendered amp, cab and pedals: the later look, **not** the target now.

If two of these disagree, the higher one wins. If a board shows something the app can't do yet, it is example data or a proposal: see "Nothing pretends" below and `SCREENS.md` §0.

---

## 1. Principles

1. **Logic / MainStage feel.** Calm dark-grey chrome, thin dark dividers, soft gradients on raised controls, inset (pressed-in) wells for fields and displays. Bright colour only where it means something (blue = selected/primary, green = healthy/playing, amber = warning/section, red = mute/danger).
2. **Desktop first, touch only on Stage.** Edit, Layout and Settings are dense mouse-and-keyboard screens (24 px controls, 26 px rows). Stage has big targets (tiles of 60 px and more).
3. **Live safety.** Mute and PANIC sit in the same place on every screen; health is always visible; Encore starts muted. Trouble shows where you already look, in plain words, never as a window over Stage.
4. **Nothing pretends.** Every control maps to code that exists. Words come from the code (see `SCREENS.md` §1 for the running-order words).
5. **No animation.** State changes are instant. Only meters, the tuner and the reconnect spinner move.
6. **Same component, same look, everywhere.** A button is the same button in Edit, Settings and the Stage toolbar (only its size varies).

---

## 2. Colour tokens

Names match `theme::colour::*`.

| Token | Value | Use |
|---|---|---|
| `window` | #161618 | app background |
| `pane` / `paneBorder` | #1D1D20 / #0A0A0B | every panel: fill, 1 px border, radius 6, plus a 1 px top inner highlight `paneHighlight` (white 4 %) |
| `paneHeaderTop → Bot` | #252528 → #202023 | pane header strip (24 px; 22 px on Stage), line under it `paneHeaderLine` #111113 |
| `toolbarTop → Bot` | #333337 → #28282B | top bar, bottom line #0A0A0B, top inner highlight white 7 % |
| `subToolbarTop → Bot` | #2A2A2E → #232326 | toolbar inside a pane (full list) |
| `text` | #E8E8EB | primary text |
| `textStrong` | #FFFFFF | text on blue, section names in tables, active tab |
| `textSecondary` | #A9A9AF | labels, help text, inactive tab |
| `textTertiary` | #8E8E95 | field labels in panes, summaries |
| `textMuted` | #85858C | pane header labels, footers, captions |
| `textDisabled` | #6E6E75 | disabled, column headers in MIDI table |
| `textDim` | #7A7A80 | numbers, already-played rows |
| `textSoft` | #C4C4C9 | dialog title text, secondary values |
| `buttonTop → Bot` / `buttonBorder` | #4A4A4F → #3A3A3E / #0E0E10 | raised button; top inner highlight white 12 %, 1 px drop shadow black 40 % |
| `pressedTop → Bot` | #252528 → #2E2E31 | pressed / toggled-on button, with inner shadow black 70 % (0,1,3 px) |
| `accent` | #4C8DF6 | focus ring (2 px), selection outline, links (hover #82AEFA) |
| `primaryTop → Bot` / `primaryBorder` | #4C8DF6 → #2E6FDC / #0B3E86 | primary button (Done, Next, problem fixes), white text |
| `selectedRow` | #2E62C4 | selected/current row in panes and stage lists; secondary text on it #D3E1FA |
| `tableSelection` | #4C8DF6 at 35 % | selected row in the full list |
| `healthy` | #30C25A | playing bar, OK text, live dots |
| `amber` / `amberSection` | #FFB340 / at 85 % | warnings; section headings and kinds; bar marks |
| `amberStrong` | #FF9F0A | warning badge fill (ink #1A1200) |
| `noticeTop → Bot` / `noticeText` | #3D2F12 → #30250E / #FFD27A | notice bar / go-to prompt |
| `danger` | #FF8A80 | danger text (PANIC label in Edit, Remove, Delete this layout) |
| `mutedTop → Bot` / `mutedBorder` | #E8483E → #BF2C25 / #6E120D | MUTED button |
| `mutedBanner` | #C9302A | "Output muted — click to go live" bar |
| `panicTop → Bot` / `panicBorder` | #A62B24 → #761A15 / #C2392F | Panic tile; "AUDIO LOST" health widget |
| `lostTop → Bot` / `lostText` | #5A1A16 → #431311 / #FFB4AD | interface-lost banner |
| `fieldFill` / `fieldBorder` | #141416 / #0C0C0E | inset field (text, values), inner shadow black 60 % (0,1,2 px) |
| `lcdTop → Bot` / `lcdBorder` / `lcdText` / `lcdLabel` | #1C232C → #12171D / #07090B / #D5E4F7 / #7F95B3 | toolbar LCD |
| `widget` / `widgetBorder` | #1C1E23 / #2B2E35 | stage widgets (radius 6) |
| `widgetTagFill` / `widgetTagText` | black 55 % / #D8D8DC | widget name tag |
| `meterTrack` / `meterGreen` / `meterYellow` | #0E0E10 / #2BC24F / #E8CF3A | level meters |
| `expressionFill` | #6EA5FA | expression bars, pedal-assigned parameter bars |
| `expChipFill` / `expChipText` | #1B2C45 / #7FC4FF | "EXP 1" chip |
| `menu` | #2C2C30 at 97 % | popup menus, border #0A0A0B, highlight #2E62C4, separators #3A3A3F, shortcuts #85858C |
| `dialog` | #232326 | dialog body; title bar #38383C → #2E2E31; footer #262629 |
| `dimmer` | black 45 % | behind modal dialogs |
| `scrollThumb` | #4A4A4F (hover #5E5E64) | scrollbars: no track |

Gear-placeholder and status-badge tokens are in `EncoreTheme.h` (`gear*`, `knob*`, `led*`, `okBadge*`, `problem*`).

Patch colours: draw `PatchModel::colour` as stored (the designs use #4C8DF6, #FF9F0A, #30C25A, #FF6B4A).

---

## 3. Typography

- **Font: Inter**, bundled with the app (`assets/fonts/Inter-{Regular,Medium,SemiBold,Bold,ExtraBold}.ttf`, OFL; Latin subset, TrueType outlines; digits are tabular so numbers never jump). Never rely on a system font. Load each weight once as a `juce::Typeface::Ptr`, keep them for the app's lifetime.
- **Sizes are CSS px = em size.** In JUCE use `FontOptions (typeface).withPointHeight (px)` if your JUCE version documents point height as the em size; otherwise `withHeight (px * 1.21)` (`theme::type::emToJuceHeight`). Check once against `reference/target/*.png` (see `JUCE-ARCHITECTURE.md` §8) and then never think about it again.
- **Uppercase labels** (pane headers, section headings, LCD labels, widget titles) are written in capitals by code and tracked: letter-spacing 0.06–0.08 em (`theme::type::*::tracking`). JUCE: `GlyphArrangement` with extra spacing, or `Font::withExtraKerningFactor (tracking)`.
- The app name is never typed: it's the logo (`assets/logo/`).

| Style (`theme::type`) | px / weight | Where |
|---|---|---|
| `body` | 12.5 / 400 | list rows, values, menu items |
| `bodySmall` | 12 / 400 | fields, popup buttons |
| `button` | 12 / 500 | button text |
| `label` | 11.5 / 400 | field labels, help |
| `caption` | 11 / 400 | footers, notes, layout-editor status bar |
| `paneHeader` | 10 / 700, caps, +0.08 em | "SET LIST", "INSPECTOR · AMP", group titles |
| `lcdValue` / `lcdLabel` | 14 / 600 and 8.5 / 700 (Stage: 16 and 9) | LCD |
| `sectionHeading` | 10.5 / 800, caps, +0.06 em | "SET 1" in the sidebar |
| `tableRow` / `tableSection` / `tableKind` | 13.5 / 400; 14 / 700; 11 / 700 caps | full list |
| `summaryLarge` | 20 / 700 | pre-show summary |
| `bannerText` | 15 / 700 | stage banners |
| Stage | tag 10.5 / 600; title 13 / 700 caps +0.08 em; current name up to 168 / 800 (−0.04 em), shrinks to fit; set list rows 20 / 700, headings 13 / 800 caps | stage widgets (all scale with the widget's text size, see `SCREENS.md` §3) |

---

## 4. Shape, depth and spacing

- **Radii:** panes and stage widgets 6, buttons and popup buttons 5, fields 4, menus 7, dialogs 10, group boxes 7, chips 3, LCD 6, scrollbar thumb 6.
- **Borders:** 1 px, always darker than the fill (#0A0A0B for panes/menus/dialogs, #0E0E10 for buttons, #0C0C0E for fields, #2B2E35 for stage widgets).
- **Depth recipe (raised):** vertical gradient top→bottom + 1 px inner top highlight (white 7–12 %) + optional 1 px drop line (black 40 %). **Inset (wells):** flat dark fill + inner shadow at the top (black 60–70 %). Panes are flat with a 4 % top highlight. Dialogs and menus get a soft drop shadow (0, 14 px, blur 40, black 60 % for menus; 0, 30, 80, black 70 % for dialogs) — use `juce::DropShadow` rendered once into a cached image, never per frame.
- **Spacing:** 8 px grid. Outer padding 8, gap between panes 8, pane header padding 10 left / 6–8 right, row padding 8, dialog padding 20–22. Rows 26 (panes), 28 (full list), 24 (menus, MIDI mapping rows).

---

## 5. Components

Each item: what it looks like, its states, and the JUCE class that draws it (see `JUCE-ARCHITECTURE.md` for the classes). Sizes are defaults; the component may be taller where noted.

### 5.1 Toolbar (`ui::kit::Toolbar`)
Height 44 (Edit, Layout) or 48 (Stage) + 1 px bottom line. Gradient `toolbarTop → Bot`, inner top highlight. Children laid out left → right with 10 px gaps (12 on Stage), 10 px side padding. Left group: logo (wordmark 17 px high, 98 wide, 4 px side margin) · Show ▾ · show name / save state · mode switch. Centre: LCD (flexible spacers both sides). Right group: actions, PANIC, MUTED.

### 5.2 Buttons (`ui::kit::Button` via LookAndFeel)
- **Default:** 24 px high, padding 10–12, radius 5, `button*` gradient, border #0E0E10, inner top highlight 12 %, drop line 40 %. Text 12 / 500 #E8E8EB, centred; optional 12 px icon left of the text with a 6 px gap.
- **Pressed / toggled on ("b-on"):** `pressed*` gradient + inner shadow; text #FFFFFF. Used for the active mode, an open menu's button, Inspector on, selected segment.
- **Primary:** `primary*` gradient, border #0B3E86, white text (Done, NEXT, Go to fix buttons on problem lines).
- **Danger text:** default button with #FF8A80 text, weight 700 (PANIC in the Edit toolbar).
- **MUTED / LIVE:** MUTED = `muted*` gradient, border #6E120D, white 700 text; LIVE = default button, #FF8A80 text "Mute" on Stage / "LIVE" state per `MainComponent` (keep the app's words).
- **Small:** 18 px high, 10.5–11 px text (Full list, Load amp…, Reset counters inside panes).
- **Hover:** gradient 6 % lighter. **Disabled:** 45 % opacity. **Focus:** 2 px #4C8DF6 ring, 1 px outside the button (keyboard focus only).
- **Icon button:** 20 × 20, transparent, icon 12 px #A9A9AF; hover fill #2E2E32 and icon #FFFFFF.
- **Segmented control:** buttons joined in one rounded group (outer radius 6, border #0E0E10, 1 px dividers #0E0E10); selected segment uses the pressed look with white text. Mode switch segments 64 × 26 (Edit/Layout) or 72 × 30 (Stage); buffer-size segments 24 high.

### 5.3 Fields and pickers
- **Field (inset):** 22 px, radius 4, fill #141416, border #0C0C0E, inner shadow, text 12 #E1E1E5, padding 8. Used for values, the patch picker in the chain header, the in-place editor (full list: fill #0E1013, border #4C8DF6, plus a 2 px #4C8DF6 25 % ring).
- **Popup button (combo box, closed):** looks like a default button (gradient), 24 px, text left, chevron-down 9 px right (`icons/chevron-down.svg`). Settings dialogs use this; Edit panes may use the inset field look with the same chevron.
- **Checkbox:** 13 px box, radius 3, accent fill #4C8DF6 with white tick when on, #141416 with #4A4A4F border when off; label 12–12.5 #E8E8EB, 6 px gap.
- **Key cap:** 10 px #6E6E75 text, 1 px #2E2E33 border, radius 3, padding 0 4 ("F6").

### 5.4 Pane (`ui::kit::Pane`)
Fill #1D1D20, border #0A0A0B, radius 6, top highlight. Optional header 24 px: gradient, line below, label (paneHeader style) left, actions right (icon buttons, small buttons, or 11–11.5 px #85858C plain text such as "6.9% CPU"). Body clips to the rounded rect. Group box inside dialogs: fill #1A1A1D, radius 7, title above it in paneHeader style.

### 5.5 Lists
- **Pane list row:** 26 px, radius 4, padding 0 8, text 12.5 #E1E1E5. Selected/current: fill #2E62C4, text white, secondary #D3E1FA. Optional leading items: 3 × 16 px playing bar (#30C25A when playing), 18 px tabular number in #7A7A80, 9 px colour square radius 2 (patches). Trailing text 11 px #85858C.
- **Section heading (sidebar):** 22 px, text bottom-aligned with 2 px padding, sectionHeading style in amber 85 %.
- **Table (full list):** header 24 px on pane-header gradient (10 px 700 caps #85858C); rows 28 px, line under each row black 40 %, every other row white 2.5 %; section rows on pane-header gradient. See `SCREENS.md` §5 for columns.
- Lists never animate selection and never fade.

### 5.6 LCD (`ui::kit::Lcd`)
Inset display: gradient #1C232C → #12171D, border #07090B, radius 6, inner shadow (0,1,4, black 65 %), 1 px bottom highlight white 5 %. Height 32 (Edit) / 36 (Stage), padding 0 14–16, items 18–20 px apart. Each item: value (lcdValue, #D5E4F7, tabular) over label (lcdLabel, #7F95B3, caps).

### 5.7 Meters (`ui::kit::MeterBar`, restyled `LevelMeter`)
Horizontal: 8 px tall track #0E0E10 radius 4; fill green #2BC24F to 80 % of the track then yellow #E8CF3A; 2 px white peak mark; value right in 11 px tabular. Scale under it: 9 px #6A6A71 tabular (−60 −48 −36 −24 −12 −6 0). Mini meters in the Stage toolbar: 4 px tracks, 80 px wide, labels 9 px 700 #85858C.

### 5.8 Menus (LookAndFeel popup menu)
Fill #2C2C30 97 %, border #0A0A0B, radius 7, padding 5, inner top highlight white 8 %, drop shadow. Item 24 px, radius 4, text 12.5 #E8E8EB, left gutter 14 px for ticks (✓ drawn as a path), shortcut right-aligned 11 px #85858C, submenu arrow ▸ #85858C. Highlight #2E62C4 with white text (shortcut #D3E1FA). Separator: 1 px #3A3A3F with 4 px above and below, inset 6. Disabled: #6E6E75. Section header (e.g. "LAYOUTS IN THIS SHOW"): 10 px 700 caps #6E6E75. Danger items (Delete this layout): text #FF8A80.

### 5.9 Dialogs (`ui::kit::DialogFrame`)
Over the dimmed window (black 45 %; the design also blurs the window behind — optional, only if it costs nothing per frame: snapshot once when the dialog opens, blur that image once). Body #232326, radius 10, border #0A0A0B, drop shadow. Title bar 30 px, gradient #38383C → #2E2E31, 12.5 / 600 #C4C4C9 centred, close ✕ 20 px at the right. Footer 48 px (#262629, top line #0A0A0B): help text left (11.5 #85858C), buttons right (Cancel default, Done primary, 84 wide). Settings tabs: 78 × 54 icon tabs centred under the title (icon 22 px over 11 px label; on: fill #3A3A3F, white).

### 5.10 Notices and banners (`ui::kit::NoticeBar`)
- **Edit notice bar:** 30 px under the toolbar, gradient #3D2F12 → #30250E, bottom line #0A0A0B, 16 px "!" badge (#FF9F0A, ink #1A1200), text 12.5 / 600 #FFD27A, optional right-hand note 11 px #C9A964.
- **Stage notice strip:** same colours, 34 px, text 15 / 700 (go-to prompt, reconnected).
- **Lost banner:** 34 px, gradient #5A1A16 → #431311, text 15 / 700 #FFB4AD, 12 px ring spinner (2 px #FF8A80 with a gap, rotating at the UI timer rate), right note 11.5 #E8A8A2.
- **Muted bar:** 28 px #C9302A, 12 / 600 white, the whole bar is a button.

### 5.11 Stage widget frame (`ui::stage::WidgetFrame`)
Fill #1C1E23, border #2B2E35, radius 6. Name tag at (6, 6): padding 1 × 6, radius 3, black 55 %, 10.5 / 600 #D8D8DC (the widget type's display name from `StageLayout`). Title line (e.g. "SONG 3   SET 1") 13 / 700 caps +0.08 em #6E7480 at (22, 34). Variants: Next button (primary gradient, border #0B3E86), Panic (panic gradient, border #C2392F), health warning (panic gradient). In the layout editor the selected widget gets a 2 px #4C8DF6 border plus a 3 px #4C8DF6 25 % halo and white 8 px handles with #2E6FDC border on the right edge, bottom edge and corner.

### 5.12 Status badges (pre-show check)
22 px circles: problem #D83A30 with white ✕; warning #FF9F0A with #1A1200 "!"; ok #1E3A24 with #4CD964 ✓. Summary badge 44 px with a radial gradient and a soft glow (cached).

### 5.13 Scrollbars
10 px track, no track fill, thumb #4A4A4F inset 2 px, radius 6; hover #5E5E64. Same in every list and viewport (`LookAndFeel::drawScrollbar`, `getDefaultScrollbarWidth` = 10).

### 5.14 Tooltips
Fill #2C2C30, border #0A0A0B, radius 5, text 11.5 #E8E8EB, padding 6 × 8. Keep every tooltip the app has today.

### 5.15 Milestone 10 and 11 components (`Source/ui/kit`, as built)
The spec is `NEW-FEATURES-M10-M11.md` §C; values are tokens in `EncoreTheme.h`. Where the build differs from the spec, this list is what exists:
- **ProgressBar**: 4 px, track #0E0E10, fill accent; repaints only when the value changes (plugin scan).
- **UtilityTile**: chain tiles that aren't gear, 84 high, radius 8, #2C2C30 → #1F1F22, border #3A3A3F; label 8.5 / 800 caps, status dot, name 10.5 / 600, caption 9. Variants by info: plugin tile (footswitch shape), IN jack tile (64, jack icon 22 at 23, caption at 52, name at 63), OUT tile (104, up to three output lines with a send bar, "+ N more"), split ("A + B" / "A only") and merge ("stereo") tiles (52). Amber state for a missing name.
- **FaultCard**: a plugin switched off or not loaded, over the plate dimmed to 45 %: amber card, lead in bold, primary and secondary buttons.
- **ParameterList**: the plugin window when the plugin has no editor; `ListBox` rows 32.
- **Fader**: vertical (OUT stage sends, 196 px track with its scale) or horizontal (Routing output level), on the send law (+6 → 0, 0 → 0.1, −6 → 0.25, −12 → 0.4, −24 → 0.6, −40 → 0.8, −∞ → 1); double-click 0 dB; at the bottom = Off.
- **MuteButton**: "M" (24 × 20, radius 4) or stage **Mute / MUTED** (52 high, radius 6, 15 / 800, text scales with the stage); muted = MUTED gradient #E8483E → #BF2C25, white.
- **Chip**: 16 high, radius 3, 9.5 / 700 caps; styles mute, amber, exp, off, green, plain.
- **OutputStrip**: an output in the bottom row: name, channels, stereo bars, M, level; compact or 170 px.
- **LevelBars**: one or two bars, horizontal or vertical, green → yellow; the stereo meters of outputs and inputs.
- **SmallKnob** (36, value arc) and **EqCurve** (the 5-band response, log 20 Hz–20 kHz, ±12 dB, cached path): the Routing view's output EQ and its row thumbnails.
- **InfoList**: read-only inspector sections (title, right note, key/value rows with a muted tail, amber or a chip, a note, one button with an icon and key cap): the IN / OUT tiles' inspector.
- **StageBackdrop**: the block stage's lit radial background with the pane's rounded bottom (`BlockPanel`, `JackStage`).
- **SegmentedControl** gains per-segment widths (Compact | Full); **Group** gains `setGroupTitle`.
- Not kit components: the Sends matrix is a `ListBox` in `RoutingView`; the Stage Outputs widget is a `StageView` widget type (`outputs`).

---

## 6. Icons

`assets/icons/*.svg`, 24 × 24 viewBox, stroke 2 (2.5 for plus/minus/chevrons, 1.8 for the tab icons), round caps, `currentColor`. Draw them as `juce::Path`s (parse once with `Drawable::createFromSVG`, extract outlines with `getOutlineAsPath()` or replace colour with `replaceColour`) and cache per size and colour. Sizes: 12 in icon buttons, 9 for chevrons in buttons, 13 in buttons with text, 22 for settings tabs, 34 for the stage lock widget. Text glyphs ✓ ✕ ▸ ◂ are drawn as paths too (Inter has → ✓ ⚠ but not ▸ ◂ ✕; never depend on fallback fonts).

| File | Where |
|---|---|
| plus, minus, chevron-up, chevron-down | sidebar list buttons (add, remove, up, down), popup chevrons |
| duplicate, trash | patches header |
| arrow-right | "Cue 1 of 8: Clean → Patch" |
| lock | lock widget |
| sliders | Control panel button |
| audio-wave, midi-port | settings tabs |
| sidebar-right | Inspector toggle |
| close, check, alert | dialog close, ticks, warning badges |

---

## 7. Logo

`assets/logo/`: `encore-wordmark-ui.svg` (#D9D9DE, the toolbar), `encore-wordmark-cream.svg` (#F3E6C4, gear — not used while gear is placeholder), `encore-icon-clef-light.svg`, `encore-app-icon-256.png`, `encore.ico` (16–256 px, for `juce_add_gui_app (… ICON_BIG …)` / the Windows resource). Toolbar: wordmark 98 × 17, first item. Never typed in a font, never stretched, never recoloured at runtime (use the fixed-colour SVGs). Full rules: the project's `design/logo/LOGO.md`.

---

## 8. Gear placeholders (amp, cab, pedals)

Real rendered gear comes later as its own feature. Until then every amp, cab and pedal is a **placeholder** with the exact size and position the art will have, so the art can drop in later without changing any layout (see `EXTENDING.md` §6, the `GearView` interface).

- **Box:** rounded rect radius 10 (pedals 14 at full size), gradient #2C2C30 → #1F1F22, border 1 px #3A3A3F (pedals 2 px), inner top highlight white 8 %, soft drop shadow.
- **Label:** block kind in caps (`AMP · NAM CAPTURE`, `CAB · IR`, `GATE`, `EQ`, `DELAY`), 700, +0.08 em, #85858C, size ≈ height / 14 (min 8). Pedal placeholders also show the block's name centred (#E8E8EB, 600), an on/off LED (#FF3B30 with glow when on, #3A1C1A off) near the top and a footswitch circle (button gradient, border #0E0E10) near the bottom.
- **Amp panel (Edit, selected amp):** box 600 × 430; control strip at (22, 44) 556 × 138, fill #26262A, border #38383D, radius 6, holding: group labels PREAMP / EQ / OUTPUT (10 / 800 caps +0.16 em #A9A9AF with 1 px #3A3A3F rules), the parameter knobs (below), the Level match toggle, and the power LED. Model plate bottom-right: 3 × 10 padding, radius 3, #3A3A3E, 11 / 800 caps #E8E8EB (e.g. "JC 120B"). Kind label bottom-centre.
- **Knob placeholder:** 64 × 64 cell: 11 tick marks on a 270° arc (−135° … +135°, majors at both ends and the top, 1.6 px; minors 1 px; #6E6E75), knob 46 px circle, radial gradient #4A4A4F → #2A2A2E, border #0A0A0B, pointer 2 × 16 px #E8E8EB. Angle = −135° + 270° × value (0–1). Label under it 10 / 800 caps +0.12 em #C4C4C9, then an EXP chip if a pedal is assigned.
- **Toggle placeholder:** 22 × 36 track radius 11, #1A1A1D, border #4A4A4F; 16 px light knob (#F2F2F4 → #B9B9BF) at the top when on, bottom when off; ON / OFF labels 8 / 800 above and below, "LEVEL MATCH" 10 / 800 caps between.
- **LED:** 16 px circle #FF3B30 with an 8 px glow when the block is on, #3A1C1A when off.
- **Cab panel:** box 230 × 300 with `CAB · IR`; caption under it 12 / 700 #E8C27A ("Cab · <IR file name>") and 12 #A5A5AB help text.
- **Chain tiles:** amp 117 × 84, cab 64 × 84, pedals 64 × 84 (the 160 × 210 pedal placeholder drawn at 0.4), "+" tile 48 × 84 with 2 px dashed #3E3E42 border, radius 8, "+" 22 px #A5A5AB. Selected tile: 3 px #4FA3FF outline 3 px outside the tile, radius 6. The chain board behind the tiles: #19191B with an inset shadow (0, 2, 6, black 70 %).
- **Stage stomp switches:** the 160 × 210 pedal placeholder scaled to the widget (0.7 in the reference), LED shows on/off, tap toggles.

---

## 9. Interaction states (all components)

| State | Treatment |
|---|---|
| Hover (mouse) | raised controls 6 % lighter; icon buttons get #2E2E32; rows get #2A2A2F only where clickable |
| Pressed | pressed gradient + inner shadow while the mouse is down |
| Toggled on / selected | pressed look (buttons), #2E62C4 (rows), 2 px #4C8DF6 (tiles, widgets) |
| Keyboard focus | 2 px #4C8DF6 ring 1 px outside, only after keyboard navigation (never on click) |
| Disabled | 45 % opacity, no hover |
| Playing (running order) | 3–4 px #30C25A bar on the left of the row; bold text in the full list |
| Cut item | 35 % opacity, struck-through name, "cut" in the number column |
| Warning / problem | amber / red text or badge as in §5.12; never blinking |

---

## 10. Copy rules

- Use the app's own strings exactly (they live in the `.cpp` files; `SCREENS.md` lists them per screen). Ellipsis "..." for actions that open something, as the code does.
- Words for the running order come from `ShowSession::getWords()` (Set list / Cue list, song / cue). See `SCREENS.md` §1.
- Uppercase is a style: store normal case, draw capitals.
- Numbers: tabular, units with a space ("20.0 ms", "48.0 kHz", "−12.0 dB" with a real minus U+2212 in displays).
