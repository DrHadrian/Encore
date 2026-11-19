# Building the design in C++ / JUCE

How the v3 design becomes real, native Encore code. Read `DESIGN-SYSTEM.md` (the look) and `SCREENS.md` (the screens) first; this file is about structure, performance and robustness.

---

## 1. Non-negotiables

1. **Native JUCE only.** Every pixel is drawn by `juce::Component::paint` / `juce::LookAndFeel` with `juce::Graphics`. No web view, no HTML/CSS/JS, no React/Electron/WebView2, no embedded browser, no screenshots of designs used as UI. `JUCE_WEB_BROWSER=0` stays. No new third-party UI libraries.
2. **The HTML in `reference/boards/` is a drawing, not code.** Read values from it; never port, embed or ship it.
3. **Audio first.** The UI runs on the message thread only and never blocks, locks or allocates on the audio thread. It reads engine state the way it does today (atomics, `StageSnapshot`, `PeakMeter`, `Block::getCpuLoad`, session calls). No new cross-thread mechanisms are needed for the design; if you think you need one, you're doing too much in the UI.
4. **Behaviour is not redesigned.** Every action, key, menu item, tooltip, confirmation and message that exists stays (`SCREENS.md` §12). The conversion changes how things look and where they sit, not what they do. Model, session and engine code is not touched except where a screen needs a read-only accessor that doesn't exist yet.
5. **No literals in UI code.** Colours, sizes and fonts come from `Source/ui/theme/` (`EncoreTheme.h` is ready in `theme/`).
6. **Fast and calm.** No animations, no fades, no per-frame allocations, repaint only what changed (§6).

---

## 2. Where the code goes

```
Source/ui/theme/
    EncoreTheme.h            tokens (copy of docs/design/theme/EncoreTheme.h)
    EncoreFonts.h/.cpp       loads the bundled Inter weights once; font(style, scale) helpers
    EncoreLookAndFeel.h/.cpp the LookAndFeel_V4 subclass (§4)
    Icons.h/.cpp             SVG icons -> cached juce::Path, drawIcon (g, Icon::plus, bounds, colour)
Source/ui/kit/               reusable pieces (§5): Pane, Toolbar, Lcd, SegmentedControl, IconButton,
                             NoticeBar, DialogFrame, MeterBar, StatusBadge, KeyCap, GearPlaceholder, KnobView ...
Source/ui/...                existing screens, restyled and re-laid-out in place
                             (MainComponent, SetList, PatchList, ChainStrip, BlockPanel, CueListEditor,
                              StageView, MidiSettings, PreShowCheckView, TunerDisplay, LevelMeter)
Source/ui/AudioSettingsView.h/.cpp   new: the Audio tab (replaces the stock AudioDeviceSelectorComponent look)
Source/ui/SettingsWindow.h/.cpp      new: Audio | MIDI tabs in one DialogFrame
Source/ui/Inspector.h/.cpp           new: the Edit inspector column (reads the selected block and patch)
Resources/ui/                        fonts, icons, logo, app icon (copied from docs/design/assets)
```

**CMake** (the repo lists sources explicitly — add every new file to `target_sources(Encore …)`; add test sources to `EncoreTests`):

```cmake
juce_add_binary_data(EncoreUiAssets
    HEADER_NAME EncoreUiAssets.h
    NAMESPACE   EncoreUiAssets
    SOURCES
        Resources/ui/fonts/Inter-Regular.ttf  Resources/ui/fonts/Inter-Medium.ttf
        Resources/ui/fonts/Inter-SemiBold.ttf Resources/ui/fonts/Inter-Bold.ttf
        Resources/ui/fonts/Inter-ExtraBold.ttf
        Resources/ui/logo/encore-wordmark-ui.svg Resources/ui/logo/encore-icon-clef-light.svg
        Resources/ui/icons/plus.svg  ...every icon...)
target_link_libraries(Encore PRIVATE EncoreUiAssets)          # and EncoreTests, for UI snapshot tests
juce_add_gui_app(Encore … ICON_BIG Resources/ui/logo/encore-app-icon-256.png ICON_SMALL Resources/ui/logo/encore-app-icon-256.png)
```

Keep `Resources/ui/fonts/OFL-and-copyright.txt` next to the fonts and mention Inter (OFL 1.1) in the README's licences section.

---

## 3. Fonts

- `EncoreFonts` (one instance, owned by the app, created before the first window): `Typeface::createSystemTypefaceFor (EncoreUiAssets::InterRegular_ttf, size)` for each weight, kept as `Typeface::Ptr`.
- `juce::Font font (theme::type::Style s, float scale = 1.0f)` returns the right weight at `s.px * scale` (see `DESIGN-SYSTEM.md` §3 for px → JUCE height; calibrate with §8). Uppercase styles return the font with `withExtraKerningFactor (s.tracking)`; callers pass already-uppercased text.
- The LookAndFeel sets `setDefaultSansSerifTypeface (regular)` so stock components (AlertWindow fields, tooltips) use Inter too.
- Fonts never come from the system. Text never depends on a font fallback: draw ✓ ✕ ▸ ◂ as paths (`Icons`).

---

## 4. EncoreLookAndFeel (one instance, set on the main window, dialogs and popup menus)

Subclass `juce::LookAndFeel_V4`; set its colour scheme and every relevant colour id from the tokens in the constructor (`ResizableWindow::backgroundColourId`, `ListBox`, `TextEditor` incl. `highlightColourId` = accent 40 %, `CaretComponent::caretColourId`, `ComboBox`, `PopupMenu`, `Label`, `ScrollBar`, `TooltipWindow`, `AlertWindow`, `TextButton`, `ToggleButton`, `Slider`). Override (signatures per the pinned JUCE 9.0.5 headers — check them, names below are the JUCE 8 family):

| Override | Draws |
|---|---|
| `drawButtonBackground`, `drawButtonText`, `getTextButtonFont` | raised / pressed / primary / danger / small buttons (DESIGN-SYSTEM §5.2). Choose the variant from a property on the button (`button.getProperties()["variant"]` = "primary", "danger", "muted", "small", "segment-left" …) so screens don't subclass buttons just for looks. |
| `drawToggleButton`, `drawTickBox` | checkboxes (§5.3) |
| `drawComboBox`, `getComboBoxFont`, `positionComboBoxText` | popup buttons / inset fields with chevron |
| `drawPopupMenuBackgroundWithOptions`, `drawPopupMenuItemWithOptions`, `drawPopupMenuSectionHeaderWithOptions`, `getIdealPopupMenuItemSizeWithOptions`, `getPopupMenuFont`, `getPopupMenuBorderSizeWithOptions` | menus (§5.8): 24 px items, ticks, shortcuts, submenu arrows, separators, danger items (item colour), section headers |
| `drawScrollbar`, `getDefaultScrollbarWidth` (10), `areScrollbarButtonsVisible` (false) | scrollbars (§5.13) |
| `drawLinearSlider`, `getSliderLayout` | the Out volume slider (4 px track, 12 px thumb) |
| `drawRotarySlider` | knob placeholder with tick scale (§8 of DESIGN-SYSTEM) — later replaced by gear art |
| `drawLabel`, `getLabelFont` | labels in Inter with the right colours |
| `fillTextEditorBackground`, `drawTextEditorOutline` | inset fields, in-place editors |
| `drawTooltip`, `getTooltipBounds` | tooltips (§5.14) |
| `drawAlertBox`, `getAlertWindowButtonHeight`, `getAlertWindowTitleFont`, `getAlertWindowMessageFont`, `getAlertWindowFont` | small dialogs (SCREENS §11) |
| `drawCallOutBoxBackground` | if callouts are used |

Popup menus: no fade-in; drop shadow from the window flag or a cached shadow; `PopupMenu::Options().withTargetComponent (button)` so they open under the button (as today).

---

## 5. Kit components (Source/ui/kit)

Small, final classes; each paints from tokens, owns no timers, and repaints only on state change. Sketches:

```cpp
class Pane : public juce::Component              // fill, border, radius, top highlight, optional header
{   void setHeader (juce::String label, juce::String rightText = {});
    juce::Rectangle<int> getContentBounds() const;   // below the header
    juce::Component& headerActions();                // place icon buttons / small buttons here
};
class Toolbar : public juce::Component           // gradient bar; left/centre/right groups laid out in resized()
class Lcd : public juce::Component               // items { value, label }; setItems() repaints only if changed
class SegmentedControl : public juce::Component  // segments, selected index, onChange; keyboard: arrows
class IconButton : public juce::Button           // Icons::Id, 20 px; tooltip; hover fill
class NoticeBar : public juce::Component         // kind {notice, lost, muted}, text, rightNote, spinner for lost
class DialogFrame : public juce::Component       // title bar, close, optional tabs, content, footer (help text + buttons)
class MeterBar : public juce::Component          // horizontal or vertical; setPeak(); scale ticks optional
class StatusBadge : public juce::Component       // problem / warning / ok, size 22 or 44
class KeyCap : public juce::Component            // "F6"
class GearPlaceholder : public juce::Component   // see EXTENDING.md §6 (GearView); kind label, name, LED, footswitch
class KnobView : public juce::Slider             // rotary; draws the placeholder knob + tick scale via the LookAndFeel
```

Screens compose these; screens never paint chrome themselves.

---

## 6. Performance rules

Encore runs during shows on laptops; the UI must cost almost nothing and never cause an audio dropout.

1. **One clock.** The existing 30 Hz `MainComponent` timer (and the existing per-dialog timers in `MidiSettings` / `PreShowCheckView`) drives all updates. Kit components get values pushed (`setPeak`, `setItems`, `setState`); they don't poll and don't start timers.
2. **Repaint only on change.** Every `set…` compares with the stored value and calls `repaint()` only when it differs (StageView's widgets already do this — keep that pattern). Meters repaint only their bar rectangle. The LCD repaints only the item that changed.
3. **Nothing allocated or built in `paint()`.** Build strings, `GlyphArrangement`s, `Path`s, `ColourGradient`s and shadow images in `resized()` or when the data changes, store them as members, and only draw in `paint()`. Shadows (dialogs, menus, pedal placeholders): render once into a cached `juce::Image` at the physical scale, redraw only on resize.
4. **Opaque where possible.** Components that fill their bounds call `setOpaque (true)` (panes' content areas, widgets, toolbar, list rows) so JUCE skips painting what's behind. Rounded components with transparent corners stay non-opaque but are small.
5. **No full-window effects per frame.** The dialog dimmer is a flat fill. If you blur the window behind a dialog, snapshot and blur once when the dialog opens.
6. **No `setAlpha` on big components, no `setBufferedToImage` on things that change every frame.** Use `setBufferedToImage (true)` only for static, expensive pieces (e.g. a gear placeholder with shadows) and invalidate on change.
7. **Lists:** `ListBox` (virtualised) for every list that can grow (running order, patches, mappings, pre-show rows, full list). Row painting uses cached fonts; no per-row components except the in-place editor.
8. **Text:** at most one `GlyphArrangement` per changing text; the stage "fit to width" size is computed when the text or bounds change, not every frame.
9. **Budgets (Release, 1920 × 1080, mid laptop):** idle Edit view < 1 % CPU on the message thread; Stage view with meters running < 2 % and no single paint over 4 ms; opening a menu or dialog < 50 ms; switching Edit ↔ Stage < 100 ms. Measure with `juce::PerformanceCounter` around `paint` in Debug and with the Windows performance tools; the 10-minute soak in CI must still pass with zero dropouts.
10. **Debug aid:** `JUCE_ENABLE_REPAINT_DEBUGGING=1` in a local Debug build shows what repaints; the idle Edit view should flash only meters and the clock.

---

## 7. Robustness rules

- Async callbacks capture `juce::Component::SafePointer` (the code base already does this everywhere; keep it).
- The LookAndFeel outlives every component that uses it: create it before the main window, call `setLookAndFeel (nullptr)` on the top-level components in their destructors, destroy it last. Same for `EncoreFonts`.
- Dialogs: keep today's ownership (`SafePointer<DialogWindow>` members closed in `~MainComponent`) and today's rule that settings windows close before what they edit goes away.
- Every screen works with **no audio device**, **no MIDI devices**, **an empty show**, **a show with 300 cues and 40 sections**, **very long names** (ellipsis in panes, shrink-to-fit on Stage), **window sizes from 640 × 480 to 3840 × 2160** and **display scales 100–200 %**.
- Keyboard: decision 0025 ("keyboard focus belongs to the show") stays — `keepKeysForTheShow`, Space/arrows step the show except while typing. New controls must not steal focus on click (`setWantsKeyboardFocus (false)` on buttons, as today); text fields take focus only while editing.
- Accessibility: every button and control has a title (`setTitle`) and the same tooltip as today; icon-only buttons get a title that says what they do ("Add song after the playing one"). Focus ring only for keyboard navigation.
- No exceptions, no asserts that can fire in Release on user data; validate indices exactly like the existing code does.

---

## 8. Checking the look (snapshot tests)

`Tests/StageViewTests.cpp` already renders components off-screen and writes PNGs when `ENCORE_UI_SNAPSHOTS` is set. Extend that:

1. Add `Tests/UiSnapshotTests.cpp` that builds each screen with a fixture show matching the board data (`SCREENS.md` gives names, patches, sections; geometry JSON gives every string) at 1440 × 900 and writes `edit.png`, `stage.png`, `full-list-typing.png`, … with the same names as `reference/target/`. Use `FakeDevice` for audio state; fill status values to match the board.
2. Run `tools/ui-compare.py <snapshots folder> docs/design/reference/target <out>` (in this kit): side-by-side, overlay and difference images per board, plus a score. Fonts render a little differently in JUCE than in Chrome, so compare by eye and by the score — layout, sizes and colours must match; anti-aliasing may differ.
3. **Font calibration (do this first):** render a label "Cue 1 of 8: Clean" at `type::body` and measure the cap height against `edit.png` (the "Cue 1 of 8: Clean" header at x 266, y 60, 14 px semibold). If JUCE's text is 21 % too large or small, switch between `withPointHeight` and `withHeight × 1.21` in `EncoreFonts` once.
4. The tests must assert content (as `StageViewTests` does: something drawn, no crash at small sizes); the image comparison is for review, not a CI gate.

---

## 9. Order of work

See `CONVERSION-PLAN.md`: theme and LookAndFeel first, then the kit, then screens one by one (Edit frame, sidebar, chain + block, inspector, bottom row, full list, Stage, layout editor, settings, pre-show, dialogs), each with its snapshot, each leaving the app working.
