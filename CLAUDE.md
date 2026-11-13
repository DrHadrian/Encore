# Encore — notes for Claude

## UI work (design system)

Encore's UI follows design v3, a native JUCE design system documented in `docs/design/` (start with `docs/design/README.md`; the agent brief is `docs/design/AGENT-PROMPT.md`). For any UI change: tokens from `Source/ui/theme/EncoreTheme.h`, components from `Source/ui/kit`, the look in `docs/design/DESIGN-SYSTEM.md`, screens in `docs/design/SCREENS.md`, rules in `docs/design/JUCE-ARCHITECTURE.md`. Never a web view or HTML/CSS. The `encore-ui` skill (`.claude/skills/encore-ui/`) has the short version.

Milestones 10 and 11 (plugins, routing): the brief is `docs/design/M10-M11-BRIEF.md`, the spec `docs/design/NEW-FEATURES-M10-M11.md`, the reference images `docs/design/reference/REFERENCES.md`.
