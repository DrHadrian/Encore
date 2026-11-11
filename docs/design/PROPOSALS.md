# Encore — proposed features and design ideas (not in the app yet)

Copy of the project doc `design/PROPOSALS.md` (2026-11-11). Use it when building future features: many already have a designed look on the v2 canvas page. **Nothing here is part of the current conversion** — the conversion builds only what the app does today.

Legend: **[Feature]** needs model or engine work · **[UI]** design only · **(in v3)** already in the v3 design · *(milestone n)* planned in the README roadmap.

## 1. Look and feel
- **[UI] Logic / MainStage pro look** (in v3). *Chosen direction.*
- **[UI] LCD status display** (in v3: item, patch, latency, rate). Tempo, key and scene later.
- **[UI] Rendered gear** (later; placeholders now): amps, cabs and pedals as pre-rendered images with knob overlays, one light from the top-left. Textures and scripts: `assets/gear-art-later/`.
  - Pedal art per block type (shared component). Still proposals: compressor (silver), overdrive (green, cream knobs), clean boost (cream), chorus "the Swirl" (black, wood sides), spring reverb (navy), input/output jack plates.
  - The EQ art shows a graphic EQ but Encore's EQ is parametric — give it its own art. The delay art shows a tape echo with Normal/Reverse but the delay has only time, feedback and mix — drop the switch or add reverse.
  - Amp Designer-style combo with gold panel, chickenhead knobs, grille, chrome corners and handle; matching 2×12 cab.
  - **[Feature] Amp skins** per amp family, from the NAM model's metadata or a "Skin" button.
- **[UI] Colour meanings**: blue = selected/primary, red LED = engaged pedal, orange = changes per scene, green = healthy/now, red = mute/danger.
- **[UI] Touch targets on Stage**: ≥ 128 px for mid-song actions, 96 px stomps, 56–64 px tuner/mute, 44 px minimum, ≥ 10 px between live targets.
- **[UI] Live-safety rules**: Mute in the same place everywhere, no pop-ups while playing, health always visible. **Muted banner** (in v3).

## 2. Stage view
- **[UI] Mode switch** Stage | Edit | Layout (in v3).
- **[Feature] Scenes**: named, coloured snapshots inside a patch (block on/off and values), footswitches A–D, without changing cue (not the running order's "scene" sections). Needs a Scenes widget, scene tabs in Edit, a per-scene value table, "differs from scene" dots, a SCENE source chip.
- **[Feature] Tap tempo** widget and footswitch, tempo-synced delay.
- **[Feature] Tempo and key per cue/song** in the LCD, Current cue and Set list.
- **[UI] Mini tuner strip widget.**
- **[UI] Next cue widget you can tap to advance.**

## 3. Edit view
- **[Feature] Undo / redo.**
- **[UI] Inspector** (in v3). Still proposals: a source chip per parameter (fixed / EXP n / scene) and "+ Add assignment".
- **[Feature] Library panel**: search amps (NAM), cabs (IR), effects, VST3 plugins; drag into the chain *(VST3: milestone 10)*.
- **[UI] "Used by N cues"** on each patch.
- **[Feature] Cab mic placement** (drag a mic over the cab; blend or pick IRs).
- **[Feature] More blocks**: compressor, drive, boost, chorus, reverb, tape echo with reverse, graphic EQ, wah.
- **[UI] Input/Output jack tiles** at the chain ends. **[UI] Chain hints**: drag to reorder, double-click to open, ⌫ remove.

## 4. Layout editor
Today: drag/resize (right, bottom, corner) on 24 × 16; double-click empty or + Widget to add; right-click Text size / Edit text / Duplicate / Bring to front / Remove; the Layout menu; Done.
Proposals: widget-panel **search**, **"placed" dot**, **drag from the list**; inspector **X/Y/W/H** and **alignment**; **[Feature]** widget content options, widget style (colour, hide tag, thin frame), widget tap actions / swipe / MIDI learn; snap and grid options; alignment guides and size badge; resize from all edges; keyboard nudge (arrows, Shift+arrows, Ctrl+D, Del); **[Feature]** lock position, fill empty space, preview on stage, a layout per screen size; layout thumbnails strip.

## 5. Settings
- One Settings window with tabs (in v3: Audio, MIDI). Proposed tabs: **Stage**, **Controllers**, **Remote**, **General**.
- Latency bar (in v3). Proposals: "under 10 ms feels instant" hint, CPU history graph.
- **[Feature] Routing**: click/cue output, per-cue input/output, FOH/IEM buses *(milestone 11)*.
- **[Feature] Test tone**, "Check input level".
- **[Feature] Stage-safety switches** (priority, throttling, limiter ceiling, preload next cue, start muted, autosave) — automatic today, read-only in v3.
- **[Feature] MIDI**: activity lights per input and per mapping, "Test your controller" panel, MIDI monitor.
- **[UI] "Mappings belong to: This computer / This show"** segmented switch (checkbox today).

## 6. Other devices
- **[Feature] Tablet remote** (compact Stage over Wi-Fi) and **phone remote** (pocket view, Next at thumb height).

## 7. Earlier v1 ideas
Perform / Edit / Setup modes with "hold Edit 1 s to unlock"; an "Up next" card; scene pads; "Stage lock on" in the toolbar. A heavy hardware look was tried and rejected for the Logic look.

## 8. Show safety (on top of milestone 9)
Open log folder button in the pre-show window; pre-show status dot in the toolbar; run the check when a show opens or on entering Stage; reconnect progress on the lost banner ("attempt 4 · 3 s"); recent events list in the pre-show window; dismissable notices with "Show in log".

## 9. Running order (on top of milestone 8)
Words everywhere ("Song 3 of 12", "Current song"); collapsible sections; a big go-to overlay on Stage; drag to reorder; optional cut items in the sidebar; print/export the running order (PDF/CSV); section path in the Edit LCD.
