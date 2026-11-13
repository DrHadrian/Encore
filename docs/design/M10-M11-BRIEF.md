# Brief: milestones 10 and 11 in design v3, built natively in C++ / JUCE

*Paste everything below the line into the Encore software chat. Attach `encore-design-kit-m10-m11.zip`, or point the chat at `docs/design/` if the addendum is already committed.*

---

You are working on **Encore** (`github.com/drhadrian/encore`), a Windows live rig for guitar (like MainStage) in **C++20, JUCE 9.0.5 and CMake**. Design v3 is already built natively (PR #12) with a token header, `EncoreLookAndFeel` and the kit in `Source/ui/kit`. Milestone 10 (VST3 plugins, PR #13) is built.

Your job has two parts:
- **A.** Restyle the milestone 10 screens to their finished design.
- **B.** Build milestone 11 (routing) to its finished design.

Both must be **native, fast, hardened JUCE code** that looks practically identical to the reference images. New pieces go into the kit, so future screens get them for free.

## Setup
1. Work on top of `milestone-10` (or main once #11 → #12 → #13 are merged). Branch: `m10-m11-design`.
2. Unzip `encore-design-kit-m10-m11.zip` at the repo root. It **adds** files to `docs/design/` and replaces `docs/design/README.md` and `CLAUDE.md`, both extended. It **replaces** three v3 targets (`settings-audio`, `settings-midi`, `preshow-check`) that now show the Plugins tab, the driver popup and the plugin line. Commit it as the first commit.
3. Read, in this order:
   1. The skill `.claude/skills/encore-ui/SKILL.md` and `CLAUDE.md`.
   2. `docs/design/README.md`, then this brief.
   3. `docs/design/NEW-FEATURES-M10-M11.md`: **the spec**. Its screens, components and tokens win on these features.
   4. `docs/design/reference/REFERENCES.md`: which image is which state, the fixtures, and the compare workflow.
   5. `DESIGN-SYSTEM.md`, `SCREENS.md`, `JUCE-ARCHITECTURE.md`, `EXTENDING.md`.
   6. From the Claude project "Encore": `claude/MILESTONE-11-PLAN.md` (§0 has the design answers, §3 the backend) and `design/HOW-ENCORE-WORKS.md`.
   7. Every image in `docs/design/reference/target/` that this work touches (the `@2x` versions for detail), and `overview-m10-m11.png`.

## The goal
- Every target listed in `REFERENCES.md` §3–4 is built and has a snapshot that, side by side, **looks practically identical**: layout, sizes, colours, Inter, words, states.
- Gear art stays **placeholders** behind `GearView` (as in the targets). `with-gear-art/` is not the target.
- The new look lives in **tokens and kit components**: ProgressBar, UtilityTile (with JackTile, PluginTile and the Split/Merge tiles), FaultCard, ParameterList, Fader (vertical and horizontal), MuteButton, Chip, OutputStrip, SendMatrix, SmallKnob, EqCurve, a stereo variant of MeterBar, and OutputsWidget. Screens only compose them.

## Hard rules (same as v3, plus the milestone 11 words)
1. **Native JUCE only.** `Graphics`, `Component`, `LookAndFeel`. No web view, HTML/CSS/JS, React, Electron or new UI library. The `.dc.html` boards are drawings to read values from: never port, embed or ship them.
2. **Don't lose or invent behaviour.**
   - Every existing action, key, menu item, tooltip, confirmation and message stays (SCREENS §12).
   - Nothing pretends: no control without its real behaviour.
   - Don't build `reference/ideas/`.
   - The spec's small additions to milestone 10 (scan progress, FaultCard, ParameterList, the hint in Settings › Plugins) are wanted. Add only the read-only accessors they need, e.g. `PluginCatalog` scan (index, total).
3. **Words.**
   - Milestone 10: from the code.
   - Milestone 11: **Output, Send, Path A / Path B, Routing**; a missing name gets **NOT HERE**; the pre-show fix button is **"Routing..."**; the pre-show texts are in plan §3.8.
   - Running-order words come from `ShowSession::getWords()`.
4. **Tokens only.**
   - No colour, size or font literal outside `Source/ui/theme/`.
   - Add the spec §C tokens under those names; their values come from the geometry JSON.
5. **Audio safety first.**
   - UI on the message thread only.
   - Nothing locks, waits or allocates on the audio thread; routing changes reach it lock-free, as chains do (plan §3.4).
   - The soak passes with zero dropouts, with several outputs and a split patch.
6. **Fast and calm.**
   - No animation and no new timers (the 30 Hz timer drives meters and states).
   - Repaint only what changed; nothing allocated or built in `paint()`.
   - Cache paths, gradients and the EQ curve (rebuild only when a value changes).
   - Use `ListBox` for lists that grow: plugins, parameters, sends rows.
7. **Robust.**
   - Works with no device, no MIDI, 0 plugins, 1 or 8 outputs, missing names and channels, long names, 640 × 480 to 4K, and 100–200 % scaling.
   - SafePointer / WeakReference in async callbacks; keyboard focus stays with the show.
   - **Live safety:** no pop-ups on Stage; Mute stays where it is.
8. Repo conventions: code style, explicit CMake source lists, the licence header in new files, `docs/DECISIONS.md` for decisions.

## How to work
**Part A, milestone 10** (spec §A; one commit each):
1. Settings › Plugins: hint line, Blocked chip and help line, ProgressBar.
2. PluginTile and the plugin block stage plate.
3. Switched-off and not-installed states: FaultCard, amber states.
4. ParameterList in the plugin window.
5. Re-snapshot `settings-audio`, `settings-midi` and `preshow-check` against the replaced targets.

**Part B, milestone 11** (plan §6; one commit per step, CI green each):
1. Backend steps 1–4.
2. UI (spec §B):
   1. Routing button with key cap F7, and the Routing view (Inputs & outputs, Sends).
   2. Chain: IN/OUT tiles, split and merge with two stacked path rows, "Move to path A / B".
   3. Send faders (OUT selected); merge and input stages.
   4. Bottom row, Compact | Full.
   5. Stage Outputs widget, and the Patch widget's input.
   6. MIDI "Mute output ▸".
   7. Pre-show lines.
3. README ("Testing milestone 11 with your guitar"), DECISIONS, PR.

**For every screen and state:**
1. Build it from the kit.
2. Add its snapshot to `Tests/UiSnapshotTests.cpp`, with the fixture from `REFERENCES.md` and **the target's file name**.
3. Render with `ENCORE_UI_SNAPSHOTS=<dir>`.
4. Run `python docs/design/tools/ui-compare.py <dir> docs/design/reference/target <out>` and look at side, overlay and diff.
5. Fix until layout, sizes, colours and words match.

Take every missing number from `reference/geometry/<name>.json`, never from the picture. If something is truly ambiguous, ask, or keep today's behaviour and match the target. Write the choice in the commit message.

When it's done, fold `NEW-FEATURES-M10-M11.md` into `DESIGN-SYSTEM.md` and `SCREENS.md`. Add the new components to the skill's kit list. Update `SCREENS.md` §12 with the new controls.

## Done means
- Every target in `REFERENCES.md` §3–4 has a matching snapshot, with the score table attached.
- The new components are in `Source/ui/kit` and documented; the new tokens are in `EncoreTheme.h`.
- CI is green: Windows Release, Debug tests, and the soak with several outputs and a split patch.
- README, `docs/DECISIONS.md`, SCREENS, DESIGN-SYSTEM and the skill are updated.

**Report after each step:**
- what changed;
- the `ui-compare.py` table;
- any differences you left on purpose, with the reason;
- anything you had to decide.
