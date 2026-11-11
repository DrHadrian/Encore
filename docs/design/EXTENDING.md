# Extending the Encore UI (future screens, settings, widgets, blocks, gear art)

The design system is meant to carry every future feature without new visual decisions. Before adding UI, check `design/PROPOSALS.md` in the project (and the v2 boards on the design canvas): many features already have a designed look there. If it's designed, build it like that, with the kit. If it isn't, follow these recipes.

---

## 1. Rules that never change

- Tokens only (`EncoreTheme.h`). If you need a new colour or size, add a named token with a comment, in the right group, and use it everywhere it applies. Never a literal in a screen.
- Kit only. If no kit component fits, add one to `Source/ui/kit` (generic, token-driven, no timers), then use it.
- Words from the code, in the same voice as the app (plain, short, sentence case, "..." for actions that open something). Running-order words via `getWords()`.
- Desktop density everywhere except Stage; Stage stays big and calm.
- Nothing pretends: never show a control that isn't wired to real behaviour.
- No animation; repaint on change; no allocations in `paint()`.

---

## 2. A new screen or mode

1. Decide where it lives: a **mode** (in the Stage | Edit | Layout switch — only for whole-window work modes), a **pane** in Edit (sidebar or centre), a **full-area view** like the full list (covers the main grid, toolbar and bottom row stay, F-key toggles it, "Back to …" button at the right of its sub-toolbar), or a **dialog** (`DialogFrame`; settings-type things go into the Settings window as a tab).
2. Use the toolbar pattern: sub-toolbar 40 px (title left, actions, summary right, close/back right).
3. Lists → `ListBox` with kit rows; tables → the full-list table style (28 px rows, header on the pane-header gradient, alternating rows).
4. Add the screen's reference snapshot to `UiSnapshotTests` with a fixture.

## 3. A new Settings tab

Add a tab to `SettingsWindow` (78 × 54, 22 px icon + 11 px label; icons in `assets/icons` style: 24 viewBox, stroke 1.8, round caps). Content: group boxes (#1A1A1D, radius 7) in a 2-column layout (664 + 320 at the reference size), labels column 130 right-aligned, controls 24 px, footer help text describing when changes apply. Planned tabs (PROPOSALS): Stage, Controllers, Remote, General. Settings that belong to the show say so in the group title ("this show"); computer settings are the default.

## 4. A new stage widget type

1. Add the type to `StageWidgetType` and its display name to `StageLayout.cpp` (that name is the widget's tag).
2. Draw it inside `WidgetFrame`: tag at (6, 6), optional title line in the widget-title style, content scaled by the widget height and `textScale`, shrink-to-fit text.
3. Add it to the layout editor's left panel in the right group (Show / Play / Monitor / Safety) with a 2-letter icon chip.
4. Its data comes in through `StageSnapshot` or the session; it repaints only when its values change.

## 5. A new block type (and its controls)

Block controls are generated from the block's parameter list, as `BlockPanel` does today, so a new block gets UI for free:
- continuous parameter → knob (placeholder knob with tick scale) + a row in the inspector's PARAMETERS
- on/off parameter → toggle placeholder + a checkbox in the inspector's BLOCK section
- `FileSlot` → "Load …" small button in the block-stage header, Model/Format rows in the inspector

Optional per-type layout table (`BlockLayouts.cpp`): group labels and order (e.g. amp: PREAMP = Input; EQ = Bass, Mid, Treble; OUTPUT = Level match, Output, power LED). Without an entry, controls sit in one row in parameter order. Add the type's label to the gear placeholder (`AMP · NAM CAPTURE`, `CAB · IR`, `GATE`, `EQ`, `DELAY`, …).

## 6. Gear art later: the GearView contract

Today every amp, cab and pedal is a `GearPlaceholder`. When the rendered-gear feature is built (the textures and scripts already exist in the project's `design/textures/`), it must drop in without touching layouts:

```cpp
class GearView : public juce::Component          // GearPlaceholder is the first implementation
{
public:
    enum class Size { chainTile, stagePedal, panel };                  // 64×84 / 117×84 tiles, 160×210 pedal, 600×430 amp panel
    virtual void setBlock (const BlockInfo&) = 0;                      // type, name, on/off, file name
    virtual juce::Rectangle<int> getControlArea() const = 0;           // where knobs/toggles/LED go (amp: 22,44 556×138)
    virtual void setOn (bool) = 0;                                     // LED / lamp
};
```

- Sizes and the control area are fixed by the design (DESIGN-SYSTEM §8); real art uses the same boxes.
- Knobs: `KnobView` draws through the LookAndFeel; the art version swaps the drawing for a 41-frame filmstrip (`chicken_strip.png`, frame = round(value × 40)) — the knob's geometry and interaction stay.
- Pre-render everything to images (at the physical scale) when loaded; never render textures live.
- The reference look for the art is `reference/with-gear-art/*.png`.

## 7. A new menu or dialog

Menus: `PopupMenu` + the LookAndFeel; shortcuts via `shortcutKeyDescription`; destructive items last, after a separator, danger colour. Dialogs: `DialogFrame`; confirmations as styled `AlertWindow`s with the destructive button in danger text and Cancel on Escape.

## 8. A new status or warning

Edit: notice bar (temporary messages) or the health pane (persistent states). Stage: notice strip (amber) or lost-style banner (red) for anything that stops sound; the System health widget for compact state. Pre-show check: add a check in `PreShowCheck` with a fix action; the view picks it up.

## 9. Touch surfaces later (tablet/phone remote)

The v1 canvas boards (Tablet, Phone) show the direction; they use the same tokens with Stage-size targets (≥ 44 px, 96 px stomps, 128 px for mid-song actions).
