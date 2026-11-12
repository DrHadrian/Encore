# Encore design kit (design v3 → C++ / JUCE)

Everything needed to build Encore's designed look natively in the app, and to keep every future screen consistent with it. This folder is the design framework: tokens, components, screens, reference images, measurements, assets, rules and a step plan.

**Start here:** `AGENT-PROMPT.md` (the brief for whoever builds it — paste it into a new chat together with this folder), then `CONVERSION-PLAN.md`.

**Milestones 10 and 11 (plugins, routing):** start with `M10-M11-BRIEF.md`, then `NEW-FEATURES-M10-M11.md` (the screens, components and tokens) and `reference/REFERENCES.md` (how to use the images and measurements). The v3 rules below apply unchanged.

| File / folder | What it is |
|---|---|
| `AGENT-PROMPT.md` | The task brief for an AI agent (or a person): goal, rules, what to read, how to check. |
| `DESIGN-SYSTEM.md` | The look: principles, colour and type tokens, shapes, every component and state, icons, logo, gear placeholders. |
| `SCREENS.md` | Every screen: layout, content, strings, behaviour, menus, dialogs, and the parity map (every current control → its new place). |
| `JUCE-ARCHITECTURE.md` | How to build it in JUCE: code layout, CMake, fonts, LookAndFeel, kit components, performance and robustness rules, snapshot tests. |
| `EXTENDING.md` | Recipes for future screens, settings tabs, stage widgets, block types, gear art (GearView), menus, warnings. |
| `CONVERSION-PLAN.md` | 12 steps from foundation to hardening, each leaving the app working; final acceptance checklist. |
| `M10-M11-BRIEF.md` | The brief for building the milestone 10 restyle and milestone 11 (routing) screens. |
| `NEW-FEATURES-M10-M11.md` | Milestone 10 and 11 screens, the new kit components and the new tokens (adds to DESIGN-SYSTEM and SCREENS; wins on these features). |
| `reference/REFERENCES.md` | Which reference image goes with which screen and state, and how to use target, geometry, boards and ideas. |
| `PROPOSALS.md` | Future features already discussed (not part of the conversion). |
| `theme/EncoreTheme.h` | All tokens as C++ constants — copy to `Source/ui/theme/`. |
| `reference/overview.png` | All target screens on one sheet. |
| `reference/overview-m10-m11.png` | The milestone 10 and 11 targets on one sheet. |
| `reference/target/` | **The target**: every v3 screen at 1440 × 900 (and `@2x`), gear shown as placeholders. |
| `reference/with-gear-art/` | The same screens with the rendered amp/cab/pedals (the later look). |
| `reference/geometry/*.json` | Position, size, colours, font, text of every visible element on each target screen. |
| `reference/ideas/` | Designs kept for comparison only (not targets, not to build). |
| `reference/boards/*.dc.html` | The design-canvas sources (HTML drawings; values only — never ship or port them). |
| `assets/fonts/` | Inter 4.0 (Regular, Medium, SemiBold, Bold, ExtraBold) as TrueType, Latin subset, tabular digits; OFL licence. |
| `assets/icons/` | The UI icons as SVG (24 viewBox, stroke, `currentColor`). |
| `assets/logo/` | Wordmark (UI grey, cream), clef icon, app icon PNG and `encore.ico`. |
| `assets/gear-art-later/` | Textures, rendered parts and their Python scripts, for the gear-art feature later. |
| `tools/ui-compare.py` | Compares app snapshots with `reference/target/` (side by side, overlay, diff, score). |
| `skill/encore-ui/SKILL.md` | The same rules as a Claude skill (also installed at `.claude/skills/encore-ui/` in the repo). |

Design canvas (all versions, editable): https://claude.ai/artifact/VJEpVteR9LgtESUkfghhNC — pages "v3 — Current features (milestones 8 to 10)" and "v3 — Milestone 11: routing (to build)". The canvas and the Claude project "Encore" (`design/*.md`) are where the design is changed; this kit is regenerated from them.

Written against `drhadrian/encore` main @ 49983b4 + PR #11 (`milestone-8` @ f3be14a), JUCE 9.0.5. The milestone 10/11 additions are written against `milestone-10` @ 6dc4f54 and `claude/MILESTONE-11-PLAN.md` (2026-11-12).
