# Brief: build Encore's v3 design natively in C++ / JUCE

*Paste everything below the line into a new chat with the coding agent. Attach `encore-design-kit.zip` (or point the agent at `docs/design/` if the kit is already committed to the repo).*

---

You are working on **Encore** (`github.com/drhadrian/encore`), a Windows live-performance guitar rig (like MainStage, for guitar) written in **C++20 with JUCE 9.0.5 and CMake**. Your job: make the app look and feel exactly like its finished design ("design v3"), as **native, fast, hardened JUCE code** — and leave behind a design framework that every future screen uses.

## Setup
1. Clone the repo. Make sure milestone 8 is in (PR #11, branch `milestone-8`): if `Source/ui/CueListEditor.cpp` doesn't exist on main, merge `origin/milestone-8` into your work branch first.
2. If the kit isn't in the repo yet: unzip `encore-design-kit.zip` at the repo root (it creates `docs/design/`, `.claude/skills/encore-ui/` and `CLAUDE.md`; if the repo already has a `CLAUDE.md`, append the kit's section to it instead of overwriting). Commit that as the first commit of a new branch `design-v3`.
3. Read, in this order: `docs/design/README.md`, `DESIGN-SYSTEM.md`, `SCREENS.md`, `JUCE-ARCHITECTURE.md`, `CONVERSION-PLAN.md`, `EXTENDING.md`. Look at every image in `docs/design/reference/target/` (and the `@2x` versions for detail). Read the repo's `README.md` and `docs/DECISIONS.md`.

## The goal
- Every screen in `reference/target/` built in the app so that, side by side, it **looks practically identical**: same layout, sizes, colours, fonts (Inter, bundled), words, states.
- **Amps, cabs and pedals are placeholders** for now (exactly as in the target images, `DESIGN-SYSTEM.md` §8), behind a `GearView` interface so real gear art can drop in later without layout changes. The rendered-art versions in `reference/with-gear-art/` are not the target.
- The look lives in a **token header + LookAndFeel + small kit of components** (`Source/ui/theme`, `Source/ui/kit`), so future pages, settings, widgets and blocks get the same look for free.

## Hard rules
1. **Native JUCE only.** Draw with `juce::Graphics`, `Component`, `LookAndFeel`. No web view, no HTML/CSS/JS, no React/Electron, no new UI libraries. The `.dc.html` files are drawings to read values from — never port, embed or ship them.
2. **Don't change behaviour.** Every action, key, menu item, tooltip, confirmation and message that exists today stays (parity map: `SCREENS.md` §12). Don't touch engine/model/session logic beyond read-only accessors a screen needs. Don't build features that aren't in the app (`SCREENS.md` §0 lists the few proposals that appear on boards — skip them; future features are in `PROPOSALS.md`, not part of this job).
3. **Audio safety first.** UI on the message thread only; no locks, waits or allocations reaching the audio thread; the 10-minute soak test must still pass with zero dropouts.
4. **Tokens only.** No colour, size or font literal outside `Source/ui/theme/` (`docs/design/theme/EncoreTheme.h` is ready to copy).
5. **Fast and calm.** No animations or fades; repaint only on change; nothing allocated or built inside `paint()`; cached gradients, paths, shadows; opaque components; the existing 30 Hz timer drives updates — no new timers. Budgets in `JUCE-ARCHITECTURE.md` §6.
6. **Robust.** Works with no audio device, no MIDI, empty or huge shows, long names, 640 × 480 to 4K, 100–200 % scaling; SafePointers in async callbacks; LookAndFeel and fonts outlive their users. `JUCE-ARCHITECTURE.md` §7.
7. **Words from the code**, including the running-order words from `ShowSession::getWords()` (Set list/Cue list, song/cue). Where a board and the code disagree on wording, the code wins unless `SCREENS.md` names the change.
8. Follow the repo's conventions (code style, comments, explicit source lists in `CMakeLists.txt`, licence header in new files, `docs/DECISIONS.md` for decisions).

## How to work
- Follow `CONVERSION-PLAN.md` step by step on branch `design-v3`; one commit (or PR) per step; after each step the app builds (Windows, MSVC, CMake presets), runs and passes `EncoreTests`.
- Start with the font calibration (`JUCE-ARCHITECTURE.md` §8) so sizes match from the first screen.
- For each screen: build it with the kit, add/extend its snapshot in `Tests/UiSnapshotTests.cpp` (fixture data = the board's data; every string is in `reference/geometry/<board>.json`), render with `ENCORE_UI_SNAPSHOTS=<folder>`, then run `python docs/design/tools/ui-compare.py <folder> docs/design/reference/target <out>` and look at the side-by-side and diff images. Fix until layout, sizes, colours and text match (anti-aliasing may differ).
- Use exact numbers from `reference/geometry/*.json` whenever a doc doesn't give one.
- If something is truly ambiguous, choose what keeps today's behaviour and matches the target image, write the choice in the step's commit message, and continue.

## Done means
The final checklist in `CONVERSION-PLAN.md` is fully ticked, every target board has a matching app snapshot, CI is green (Windows Release + Debug tests + soak), and `README.md` / `docs/DECISIONS.md` are updated (Inter licence; "UI is native JUCE with a token-driven LookAndFeel; gear art behind GearView").

When you finish a step, report: what changed, the snapshot comparison table from `ui-compare.py`, and anything you had to decide.
