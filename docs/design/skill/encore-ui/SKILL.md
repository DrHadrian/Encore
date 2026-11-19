---
name: "encore-ui"
description: "Use for any UI work in the Encore repo (C++/JUCE): screens, settings, stage widgets, menus, dialogs, block controls, the amp builder, so they follow the Encore v3.2 design system natively in JUCE."
---

# Encore UI (design v3.2, native JUCE)

Encore's UI follows one design system. The full kit is in `docs/design/` of the repo (start with `docs/design/README.md`), also in the Claude project "Encore" under `design/kit/`. The current design is **v3.2** (16–18 Nov 2026): v3 + the v3.1 audit fixes + `design/kit/V3.2-CHANGES.md`, which wins over the other kit docs where they differ until it is merged into them. This skill is the short version; the docs win if they differ.

## Before you change UI
1. Read `docs/design/DESIGN-SYSTEM.md` (look), `docs/design/SCREENS.md` (screens, strings, parity map) and `docs/design/JUCE-ARCHITECTURE.md` (structure, performance, robustness).
2. Read `design/kit/V3.2-CHANGES.md` (or its merged copy in `docs/design/`): v3.1 tokens, the Settings button, the bottom row (collapsed + mixer view), the status bar, the menus, layout-editor drag and drop, the amp builder, new components, tokens, words, open questions.
3. Open the target image for the screen in `docs/design/reference/target/` and its measurements in `docs/design/reference/geometry/<screen>.json`; for v3.2 changes, the B… boards on the canvas page "v3.2 — Working copy".
4. For a new feature, check `docs/design/PROPOSALS.md` (it may already be designed) and follow `docs/design/EXTENDING.md`.
5. Plugins (milestone 10) and routing (milestone 11): `docs/design/NEW-FEATURES-M10-M11.md` is the spec (it wins on these features), `docs/design/reference/REFERENCES.md` maps every state to its target image and fixture, `docs/design/M10-M11-BRIEF.md` is the brief.
6. Amp builder: `design/AMP-BUILDER.md` (decisions, rules, parts, "Building it"), `design/textures/AMP-BUILDER-LAYERS.md`, `design/textures/render_amp_parts.py`, and the board source `design/boards/BAmpBuilder.dc.html` for every number.

## Rules
- **Native JUCE only**: `Graphics`, `Component`, `LookAndFeel`. Never a web view, HTML/CSS/JS, React or a new UI library. The `.dc.html` boards are drawings to read values from, never code to port.
- **Tokens only**: colours, sizes, fonts come from `Source/ui/theme/EncoreTheme.h` (+ `EncoreFonts`). No literals in screens. New value → new named token.
- **Kit only**: compose `Source/ui/kit` components (Pane, Toolbar, Lcd, SegmentedControl, IconButton, NoticeBar, DialogFrame, MeterBar, StatusBadge, KeyCap, GearPlaceholder/GearView, KnobView, Group, RowList) and `EncoreLookAndFeel`. Missing piece → add a generic kit component. Milestones 10/11 add: ProgressBar, UtilityTile (JackTile, PluginTile, Split/Merge), FaultCard, ParameterList, Fader (vertical/horizontal, send law), MuteButton, Chip, OutputStrip, SendMatrix, SmallKnob, EqCurve, stereo MeterBar, OutputsWidget. v3.2 adds: StatusBar (replaces the toolbar LCD; `Lcd` stays for the tuner), MixerStrip, InputStrip, PaneTab, HealthStatus, DropBox, AmpFace, AmpFaceView.
- **Inter, bundled**; uppercase labels tracked; tabular numbers, slashed zero in readouts; icons as cached paths; the logo is the wordmark SVG, never typed.
- **Behaviour is sacred**: never drop or change an action, key, menu item, tooltip or message. Words from the code; running-order words from `ShowSession::getWords()`. Routing words: Output, Send, Path A / Path B, Routing (F7, in Edit); a name missing on this computer → amber chip NOT HERE; pre-show fix button "Routing...". Settings: one "Settings…" button opens Audio | MIDI | Plugins on the last-used tab; every old shortcut to a tab still works.
- **Nothing pretends**: no control without real behaviour behind it. On amp faces a knob drives Input, Bass, Mid, Treble or Output or is **Fixed** ("Fixed at 6 in this capture"); a switch drives Level match or is Fixed; a light lights while the block is on or is always off; a knob that is neither is NOT SET and can't be saved.
- **Gear**: amps, cabs and pedals keep the GearView contract (amp 600 × 430 in Edit). Placeholders stay until gear art exists; an amp built in the amp builder shows its face through `AmpFaceView`, composed once to a cached image (never in `paint()`, never live on stage). The logo on any amp is the Encore logo, never a brand logo.
- **Adrian's standing decisions**: no blue accent bars or underlines as selection indicators; no light outlines on fields (they keep #0C0C0E); keep the amps and pedals; no running-order progress bar; no card in a card, even spacing; don't oversimplify (robust PC software, not a tablet app); the v4 pages are parked (only the status bar and menus were taken into v3.2).
- **Performance**: no animation; repaint only on change; nothing allocated or built in `paint()`; cache gradients/paths/shadows/images; opaque where possible; the existing 30 Hz timer drives updates (no new timers); `ListBox` for growing lists.
- **Audio safety**: UI on the message thread; never block or allocate on the audio thread; image decoding and face composing off the audio thread; soak test stays at zero dropouts.
- **Robustness**: SafePointer in async lambdas; LookAndFeel/fonts outlive users; works with no device, no MIDI, empty or huge shows, long names, 640×480–4K, 100–200 % scaling; keyboard focus stays with the show (decision 0025).

## Quality bar (research, Nov 2026)
Use this to review screens and to design new ones. It never overrides the docs, the target images or "Behaviour is sacred": if a rule here conflicts with them, write it up as a proposal for `docs/design/PROPOSALS.md` instead of changing it silently.

A screen is done only when it passes all three gates:
1. **Floor (must)**
   - Every control has real behaviour.
   - Every state is drawn: default, hover, pressed, selected, disabled, focused, warning/error, empty, loading.
   - Contrast: text at least 4.5:1, and controls, focus rings, meters and status marks at least 3:1 against what they sit on (WCAG 2.2 SC 1.4.3 / 1.4.11; the maths is the same for dark UIs).
   - Status is never shown by colour alone; add a word, icon or shape.
   - Keyboard: logical tab order; F6 cycles regions; arrow keys move inside composite controls (lists, segmented controls, matrices); focus is always visible; shortcuts appear in tooltips and menus. JUCE draws its own controls, so all of this has to be built by hand (Microsoft Learn, "Keyboard accessibility").
2. **Stage (must)**
   - Readable at a glance from about 2 m.
   - Works in low light: the brightest thing on screen is what needs attention.
   - A wrong click or tap in Stage mode cannot break the show: show-changing or destructive actions are guarded or undoable.
   - Nothing in the show path is blocking or modal (Blender HIG: non-blocking, non-modal).
   - State is shown, not hidden behind an interaction.
3. **Finish (should)**
   - Calm UI: every border, divider, gradient, number and colour encodes information or goes.
   - One accent per area.
   - Density earns its space (value per time and space, Ström-Awn).
   - Optical alignment; nested radii are concentric (inner radius = outer radius minus padding).
   - Numeric readouts use Inter tabular figures, and slashed zero where 0 and O could be confused.

**New tokens.** Each elevation (window → pane → raised) is one step lighter. Hover, selected and pressed are fixed steps from the base, the same everywhere (IBM Carbon model). Derive new colour ramps perceptually (OKLCH/Oklab), then store them as named tokens in `EncoreTheme.h`.

**Slop to reject:**
- decoration that encodes nothing
- uniform card grids
- placeholder or fake numbers and content
- invented labels instead of words from the code
- generic copy
- glows and blurs
- one-off colours, and gradients beyond the tokenised ones
- emoji

**Self-review before finishing.** Critique the screen against the three gates and list each failing item specifically. Fix the failures, then check again.

## Audit mode
When asked to audit, or to rework the UI "according to the research", list findings first and change nothing.
- For each screen in `SCREENS.md`, write one line per finding: screen · problem · gate item broken · proposed fix · risk to behaviour.
- Sort floor failures first, then stage, then finish.
- Then fix in small batches chosen by the user. Each batch ends with snapshots and `EncoreTests`.

## Key numbers (v3.2)
Window #161618 · pane #1D1D20 border #0A0A0B radius 6 · pane header 24 px #252528→#202023, 10 px bold caps #9A9AA1 · textMuted #9A9AA1, textTertiary #A3A3AA, textDim #8C8C93 · toolbar 44 (Stage 48) #333337→#28282B · buttons 24 px radius 5 #4A4A4F→#3A3A3E, pressed #252528→#2E2E31, primary #3570DA→#2860C6 · selection #2E62C4, accent #4C8DF6 (focus, selection) · healthy #30C25A · amber #FFB340 · danger text #FF8A80, MUTED #D23C33→#B02A23, panic #A62B24→#761A15 · status bar #121214 border #2A2A2F radius 7, label 8.5/700 caps #8C8C93 above value 14/600 #F2F2F4 · LCD (tuner only) #1C232C→#12171D text #D5E4F7 · menus rgba(35,35,39,0.98) radius 10, inner ring white 6 %, items 24 px radius 6, highlight #2E62C4, separators #34343A · fields #141416 border #0C0C0E · group box #29292D · scroll thumb #66666D · stage widget #1C1E23 border #2B2E35 · rows 26 (full list 28) · bottom row 104 (mixer view 196) · text 12–12.5 px Inter, labels 11.5, captions 11.

## Check your work
Add the screen to `Tests/UiSnapshotTests.cpp`, render with `ENCORE_UI_SNAPSHOTS=<dir>`, compare with `python docs/design/tools/ui-compare.py <dir> docs/design/reference/target <out>` (or with the v3.2 boards where no target image exists yet); layout, sizes, colours and words must match. Do a keyboard pass on the changed screen (Tab, Shift+Tab, F6, arrow keys, Esc) and check the three gates. Build and run `EncoreTests` (incl. soak) before committing.
