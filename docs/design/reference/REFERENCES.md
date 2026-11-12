# Reference images and measurements: how to use them

This folder is how you check that a screen looks like the design. It works the same way for every milestone (the v3 conversion used it first). This file lists the milestone 10 and 11 references and explains how to use every kind of file in here.

## 1. What each folder is
| Folder | What it is | How to use it |
|---|---|---|
| `target/<name>.png`, `@2x` | **The target.** The design at 1440 × 900 (the bottom row board is 1440 × 760). Gear art is drawn as placeholders, exactly as the app must draw it today. | Your snapshot must look practically identical: layout, sizes, colours, words, states. `@2x` is for checking small details (1 px lines, 9 px text). |
| `geometry/<name>.json` | Every visible element of the target: `t` (tag), `x y w h` (CSS px at 1×), `text`, `font` ("12px 500"), `color`, `bg` / `bgImage` (gradients), `border`, `radius`, `shadow`, `role`, `label`. | **The numbers.** When a doc doesn't give a size, colour or string, take it from here. Search by `text` to find an element. Never estimate a value from the picture when it's in the JSON. |
| `with-gear-art/<name>.png` | The same screens with the rendered amp, cab and pedals. | **Not the target.** It shows the later look for the GearView feature only. |
| `boards/*.dc.html` | The design-canvas sources (HTML drawings). | For values only: read a style or the data list in the script. Never port, embed or ship them, and never write CSS. |
| `ideas/` | Designs kept for comparison, not to build (e.g. sends as horizontal rows). | Don't build them. `ui-compare.py` ignores them because they're not in `target/`. |
| `overview.png`, `overview-m10-m11.png` | All targets on one sheet. | For orientation. |

## 2. The workflow for every screen and state
1. **Read** the spec section (`NEW-FEATURES-M10-M11.md`, `SCREENS.md`) and look at the target (`@2x` too).
2. **Build** it natively: tokens from `EncoreTheme.h`, components from `Source/ui/kit`, `EncoreLookAndFeel`. If a value is missing from the tokens, add a named token (the value comes from the geometry JSON).
3. **Snapshot.** Add the screen and state to `Tests/UiSnapshotTests.cpp`:
   - Use a fixture show with the board's data (table below; every string is in the geometry JSON).
   - Render at the target's size and write the PNG with **exactly the target's file name** (e.g. `m11-routing-sends.png`).
   - Use `FakeDevice` for the audio state and fill the status values to match the board.
4. **Render:** `ENCORE_UI_SNAPSHOTS=<dir>` while running `EncoreTests` (the snapshot tests only).
5. **Compare:** `python docs/design/tools/ui-compare.py <dir> docs/design/reference/target <out>`.
   - Open `<name>-side.png` (app | design), `-overlay.png` (doubled edges = wrong position or size) and `-diff.png` (bright = different).
   - A mean difference under about 6 is usually "same layout, different anti-aliasing".
   - Fonts render slightly differently in JUCE than in Chrome. Layout, sizes, colours and words must match; anti-aliasing may differ.
6. **Fix and repeat** until it matches. Then report the score table and any difference you left on purpose, with the reason.

Rules that always hold:
- The words come from the code (milestone 11: from the plan's §0).
- A board never adds behaviour: a control must do what its doc says, or it isn't built.
- A target never overrides the live-safety, performance or audio rules (JUCE-ARCHITECTURE).
- If the target and a doc disagree, the doc wins; say so in the report.

## 3. Milestone 10 references (plugins; restyle what's built)
| Target | Board | Screen and state | Fixture (all strings in the geometry) |
|---|---|---|---|
| `edit-plugin` | V3EditPlugin | Edit, the plugin block selected, Add block menu open from the + tile: Plugin ▸ Valhalla DSP ▸ | The v3 Edit show ("My Show", cues Clean…Last Call). Chain: Noise gate, Amp JC 120B, Cab 2×12, plugin ValhallaRoom (Valhalla DSP, VST3, v2.0.0, running, 2.1 % CPU), Delay. Stomp 3 → plugin on/off; EXP 1 → Mix. Catalog makers: Eventide, FabFilter, Valhalla DSP, Other. |
| `edit-plugin-switched-off` | V3EditPluginOff | Notice bar; plugin switched off by the crash guard (too slow); a second plugin (H9 Shimmer, Eventide) not installed; FaultCard; Inspector status amber with **Turn back on** | As above, with fault `tooSlow`. The notice text is the code's. |
| `settings-plugins` | V3Plugins | Settings › Plugins **while scanning** (14 of 62), ValhallaRoom selected, folder 2 selected, 3 blocked | 12 plugins (names, makers and versions in the geometry); folders `C:\Program Files\Common Files\VST3`, `D:\Plugins\VST3`; blocked CrashyVerb.vst3, OldSynth.vst3, BigLoader.vst3 with their reasons. |
| `plugin-window` | V3PluginWindow | The plugin's window when it has no editor: ParameterList | 13 parameters of ValhallaRoom (names and texts in the geometry). Behind it the app is drawn as ghost panes: snapshot only the window component, at its own size, and compare it with the window region of the target. |
| `settings-audio` | V3Audio | **Replaces** the v3 target: driver type is a popup; 3 tabs | As the v3 Audio fixture. |
| `settings-midi` | V3Midi | **Replaces** the v3 target: 3 tabs | As the v3 MIDI fixture. |
| `preshow-check` | V3PreShow | **Replaces** the v3 target: new first problem "1 plugin not playing" with **Plugin settings...** | v3 pre-show fixture plus one plugin problem ("Lead: H9 Shimmer isn't installed on this computer"). Update the existing snapshot fixture. |

## 4. Milestone 11 references (routing; to build)
Common fixture:
- **Show:** cues Clean, Dual amp intro (PC 2), Opener, Ballad in D, Shuffle, Rock Medley (PC 7 OUT), Closer, Last Call. Patches Clean, Dual amp, Crunch, Acoustic, Edge, Lead.
- **This computer:**
  - inputs Guitar (1, "Input 1 (Mic/Inst)") and Acoustic (2);
  - outputs Front of house (1/2), In-ear (3/4, −6 dB), Amp DI (5, **muted**);
  - interface Focusrite USB ASIO, 4 in, 4 out, so Amp DI's output 5 is missing.
- **The show also uses** "Monitor wedge", which isn't on this computer.

| Target | Board | Screen and state |
|---|---|---|
| `m11-routing-inputs-outputs` | M11RoutingIO | Routing view (F7), **Inputs & outputs**, In-ear selected: EQ curve + 6 knobs, delay ms ≈ m, limiter. Amp DI amber ("needs output 5"); names this show uses with NOT HERE + Add buttons. |
| `m11-routing-sends` | M11RoutingSends | Routing view, **Sends**: the patches × outputs matrix, with the Monitor wedge column NOT HERE and Amp DI muted. |
| `m11-edit-split-merge` | M11Merge | Dual amp: IN, gate, SPLIT (A + B), path A (amp + cab) over path B (amp + cab), MERGE selected (stereo), delay, +, OUT. Merge Level/Pan A and B on the block stage; bottom row compact. |
| `m11-edit-out-sends` | M11Sends | OUT selected: 4 vertical send faders: Front of house 0 dB, In-ear −6 dB, Amp DI (OUTPUT MUTED), Monitor wedge (NOT HERE). |
| `m11-edit-in-missing` | M11Input | IN selected: the patch uses an input name not on this computer (amber, NOT HERE, plays input 1 instead). |
| `m11-bottom-row` (1440 × 760) | M11BottomRow | A sheet of bottom-row variants: 3 outputs compact (the default), 8 compact, 8 full (scrolling). Snapshot each variant as its own image at its row's size and compare it with the matching band of the target. |
| `m11-stage-outputs` | M11Stage | Stage with the **Outputs** widget (meter + Mute/MUTED per output; Amp DI MUTED) and the Patch widget showing its input. |
| `m11-preshow-check` | M11PreShow | Pre-show with the routing lines (plan §3.8) and **Routing...** buttons. |
| `m11-settings-midi-mute-output` | M11Midi | MIDI tab: action menu open at **Mute output ▸** (submenu of outputs); a binding to "Monitor wedge" in amber. |
| `ideas/idea-m11-sends-rows` | M11SendsRows | **Not to build.** Sends as horizontal rows, kept for comparison. |

## 5. When there is no reference
For a state no board shows (0 outputs, no device, very long names, 640 × 480, 200 %):
- Follow `EXTENDING.md` and build it from the same components and tokens.
- Add a snapshot named `<screen>-<state>.png`. It has no target, so ui-compare lists it as missing a target.
- Say in the report that it was designed from the rules.

Ask the design chat when a new state needs a real design decision.

## 6. Regenerating the references
The references are made from the design canvas, not by hand:
- The design chat renders every board with headless Chrome in two modes. **reference** (with gear art) goes to `with-gear-art/`. **placeholder** goes to `target/`, with an extra render at 2× scale for `@2x`.
- It also dumps every visible element to `geometry/`.

When a board changes, all three are replaced together. Never edit a target image or a JSON by hand.
