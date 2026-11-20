# Milestone 12 target pictures: the plugin sandbox

Made 19 Nov 2026 for `claude/PROMPT-DESIGN-TODO.md` (A4), after Adrian's decisions (`claude/ROADMAP.md` §3): a **per-block switch "Run in its own process"**, off by default and **turned on by itself** for a plugin that crashed or hung before; **one helper process per plugin**. All words below were **approved by Adrian on 19 Nov**.

**Boards:** canvas page "Milestone 12 — plugin sandbox (to build)": **M12Plugin** (Edit v3.2 with a VST3 block; prop `state` = inproc | sandboxed | auto | crashed | hung | late) and **M12PreShow**. M12Plugin is BEditV32 with the plugin block from the v3 board "Edit — plugin switched off by the crash guard", brought to the v3.1 tokens.

**Format:** as `design/reference/v32/README.md`: 1x and @2x PNG, `m12-<name>.json` (CSS px at 1x, relative to the picture). The pictures and JSON are in `m12-targets.zip`; in the repo they go with the others under `docs/design/reference/target/` + `geometry/`.

## Files

| File | State |
|---|---|
| `m12-plugin-inproc` | Running inside Encore, switch off (today's behaviour), with the hint |
| `m12-plugin-own` | Running in its own process: switch on, cost line, tile marker, header "· own process" |
| `m12-plugin-own-auto` | The same, switched on by itself because the plugin crashed Encore before |
| `m12-plugin-crashed` | Its process crashed: restarting, playing dry; fault card, notice, amber tile |
| `m12-plugin-hung` | Its process stopped answering: restarting, playing dry |
| `m12-plugin-late` | Answered too late 3 times in a row: switched off, Turn back on |
| `m12-preshow` | Pre-show check with "Plugin restarted" (to look at). "1 plugin in its own process" (fine) is the last line; it's below the visible part at this size, as in the app the list scrolls |

The H9 Shimmer tile next to it is amber in every picture because it isn't installed (the v3 board's state). It's not part of the sandbox.

## Inspector › Plugin (Block group)

- **Status** (as today, new texts): "Running" · "Running in its own process" (green #30C25A) · "Restarting after a crash (playing dry)" · "Restarting: it stopped answering (playing dry)" · "Answered too late and was switched off" (amber #FFB340). **Turn back on** shows only in the last state (as for the crash guard today). Open plugin window… stays.
- **New row "Process"** (label column as the others; label top-aligned, padding-top 2):
  - a checkbox (the "Level match" style, accent #4C8DF6) **"Run in its own process"**. Tooltip: "Runs the plugin in a helper process. A crash or hang there can't stop Encore: the block plays dry while it restarts."
  - when on, the **cost line** under it, 11 px #A3A3AA, tabular + slashed zero: "+0.4% CPU · +0.7 ms per buffer · 0 played dry" (the helper's CPU, the measured round trip per buffer, and the buffers played dry since the last reset).
  - when off, the hint: "Runs inside Encore. Turn on for a plugin that ever crashed or hung."
  - when it was switched on by itself: "Turned on by itself: this plugin crashed Encore before." under the cost line.
- The row sits between the status buttons and Stomp switch.

## Chain tile and block stage

- **Tile:** a 10 px "process" icon (two overlapping rounded squares, stroke 2.6, #A3A3AA) at the top right, left of the status dot, while the block runs in its own process. Tooltip "Runs in its own process". Dot and border as today: green running; amber (#FFB340 dot, border, name #FFD27A) while restarting or switched off.
- **Block stage header:** "Plugin · VST3 · ValhallaRoom · own process" while sandboxed; the dot follows the tile.
- **While restarting or switched off:** the placeholder dims to 45 % and the FaultCard sits on it (the existing kit FaultCard).

## FaultCard and notice texts

| State | Card (bold lead, then the rest) | Buttons | Notice bar (amber, under the toolbar) |
|---|---|---|---|
| Process crashed | **Restarting:** its process crashed. The signal passes through it dry until it's back, so the show goes on. | none | Clean: ValhallaRoom's process crashed and is restarting (playing dry) |
| Process hung | **Restarting:** it stopped answering. The signal passes through it dry until it's back, so the show goes on. | none | Clean: ValhallaRoom stopped answering and is restarting (playing dry) |
| Late too often | **Switched off:** it answered too late 3 times in a row. The signal passes through it dry, so the show goes on. | Turn back on · Open plugin window… | Clean: ValhallaRoom answered too late and was switched off |

"Clean" is the patch, "3" is `PluginBlock::overrunsBeforeSwitchingOff`. When the restart works, the card goes away and the notice follows the normal notice timing. On Stage, the same notice text uses the stage notice line (no pop-up).

## Plugin window

It opens from the helper as its own native window (no frame of ours; embedding across processes isn't reliable on Windows). Title as today, "<plugin> — <patch>", plus " · own process": "ValhallaRoom — Clean · own process". While the helper restarts, the window closes; it doesn't reopen by itself.

## Pre-show check

- To look at: **"Plugin restarted"**, detail "Clean: ValhallaRoom's process crashed 1 time since the last reset. It played dry while it restarted." (or "stopped answering"), fix button **Reset counters**.
- Fine: **"1 plugin in its own process"** ("N plugins …"), detail "Clean: ValhallaRoom · +0.4% CPU, +0.7 ms" (the total cost when several).

## Settings › Plugins

Nothing new: the switch is per block (decision B1.1), so there's no per-computer setting.
