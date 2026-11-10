# Encore

A live rig host for guitarists on Windows: guitar in → amp + effects → out, with
patches organised into set lists and cue lists, switched from a MIDI foot
controller. Built for every kind of gig, from a 12-song bar set to a 500-cue
musical.

- **Stability first**: no allocations, locks or file I/O on the audio thread;
  everything a show needs is loaded before the show starts.
- **Native NAM amps** via [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore),
  a built-in cab IR loader, and VST3 hosting for everything else.
- **MainStage-style use**: a clean stage view and a separate edit view.

## Status

Milestone 0: device setup and dry guitar passthrough. Not usable on stage yet.

| Milestone | What you can test |
| --- | --- |
| **0 – Toolchain and passthrough** ✅ | Dry guitar through ASIO, latency display, meters, CPU/dropout counters, master volume and mute |
| 1 – Real-time core | Gate and EQ blocks, click-free toggles, limiter |
| 2 – NAM amp block | Play through .nam captures |
| 3 – Cab IR block | Amp + cab |
| 4 – Data model and saving | Patches and shows saved to a self-contained folder |
| 5 – Patch switching | Gapless switches with tail spillover |
| 6 – MIDI | Foot controller, expression pedals, MIDI out |
| 7 – Performance view and tuner | Stage view, lock, panic |
| 8 – Theatre cue lists | Acts/scenes/cues, insert, renumber, cut, go to |
| 9 – Robustness | Crash recovery, reconnect, pre-show check, soak test |
| 10 – VST3 hosting | Plugins with crash guard |
| 11 – Routing | FOH/IEM buses, per-cue input/output |
| 12 – Plugin sandbox | Risky plugins in their own process |

## Building (Windows)

You need **Visual Studio 2022 or newer** with the *Desktop development with C++*
workload (it includes CMake and Ninja), and **Git**.

### In Visual Studio

1. *File → Open → Folder…* and pick the `encore` folder.
2. Visual Studio reads `CMakePresets.json`. Choose **Debug (x64)** while
   developing, or **Release (x64)** to play (Debug builds are much slower).
3. The first configure downloads JUCE (pinned version, a few minutes). Later
   configures reuse it.
4. Pick **Encore.exe** as the startup item and press F5.

### From a terminal

From a *Developer PowerShell for VS*:

```powershell
cmake --preset release
cmake --build --preset release
.\build\release\Encore_artefacts\Release\Encore.exe
```

If you already have a JUCE checkout, add `-DENCORE_JUCE_DIR=C:/path/to/JUCE` to
skip the download.

Every push is also built on GitHub Actions; the built `Encore.exe` is attached to
each run as an artifact.

## Testing milestone 0 with your guitar

1. Start Encore. It always starts **muted** (red button). That's deliberate.
2. *Audio settings…* → pick your **ASIO** driver, tick your guitar input and the
   outputs you listen on, and set the buffer to 64 or 128 samples.
3. Choose the guitar input in **Guitar input** and play. The input meter
   should move, peaking around −18 to −6 dBFS. If it clips, turn down the
   interface's input gain.
4. Set **Master volume** low, then click **MUTED** to go **LIVE**.
5. Check:
   - you hear the dry guitar with no crackles while you play for a few minutes;
   - the round-trip latency shown matches the buffer you chose (64 samples at
     48 kHz is usually 3–6 ms in total, depending on the driver);
   - **overruns** and **driver dropouts** stay at 0. If they climb, try a larger
     buffer and tell us the CPU figure;
   - moving the volume or muting never clicks;
   - closing and reopening Encore remembers your device, input and volume.

## Project layout

```
Source/
  Main.cpp            app start-up, main window
  app/                windows, settings (per-machine, in %APPDATA%\Encore)
  engine/             everything that runs on or talks to the audio thread
  ui/                 reusable widgets
docs/DECISIONS.md     short log of important design decisions
```

## Licence

Encore is free software under the **GNU Affero General Public License v3**
(see [LICENSE.md](LICENSE.md)). It uses JUCE under its AGPLv3 option, the
Steinberg ASIO SDK headers bundled with JUCE (GPLv3 option, compatible with
AGPLv3), and, from milestone 2, NeuralAmpModelerCore (MIT).

ASIO is a trademark of Steinberg Media Technologies GmbH.
