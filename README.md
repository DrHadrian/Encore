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

Milestone 5: gapless patch switching with delay tails ringing out, a set list
stepped from the keyboard, every patch preloaded; shows saved to disk; NAM amps,
cab IRs, gate, EQ, delay, safety limiter. Foot controller (MIDI) comes next.

| Milestone | What you can test |
| --- | --- |
| **0 – Toolchain and passthrough** ✅ | Dry guitar through ASIO, latency display, meters, CPU/dropout counters, master volume and mute |
| **1 – Real-time core** ✅ | Gate and EQ blocks, click-free toggles and sweeps, safety limiter |
| **2 – NAM amp block** ✅ | Play through .nam captures, swap amps while playing |
| **3 – Cab IR block** ✅ | Amp + cab, swap IRs while playing |
| **4 – Data model and saving** ✅ | Patches and shows saved to a self-contained folder, edit view |
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
3. The first configure downloads JUCE, NeuralAmpModelerCore and Eigen (pinned
   versions, a few minutes). Later configures reuse them.
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

Every push is also built on GitHub Actions and the tests are run there; the built
`Encore.exe` is attached to each run as an artifact.

### Tests

`EncoreTests` checks the engine without any audio hardware: filter responses,
gate and limiter behaviour, click-free bypass, the chain handover, sample-rate
conversion, loading and playing real NAM captures, cab IRs with zero latency,
the show file format and folders (round trips, backups, portability), building
patches into chains and capturing edits back, the session (edit, save, reopen),
patch switching (mathematically seamless, tails ringing out, safe freeing of
removed patches), and that processing, switching and swapping amps or IRs never
allocates memory. In Visual Studio use *Test → Run CTests for
Encore*, or from a terminal:

```powershell
cmake --build --preset debug --target EncoreTests
ctest --preset debug
```

## Live performance settings (Windows)

At start-up Encore opts out of Windows power throttling, raises its priority,
puts its audio thread in Windows' *Pro Audio* scheduling class and keeps the
computer and screen awake. The status line under the latency shows what took
effect. For shows also:

- **plug in the charger**: on battery Windows saves power at the audio's
  expense (the status line warns you);
- set Windows' power mode to **Best performance**;
- use 64 samples or more, so there is headroom for spikes.

## Testing milestone 5 with your guitar

1. **Set up two patches**, e.g. *Clean* and *Lead*. Add a **Delay** to *Clean*
   (the **+** tile → *Delay*; try 400 ms, 50 % feedback, 40 % mix).
2. **Set list**: with *Clean* playing, click **+ Add** in *Set list*; select
   *Lead* in *Patches* and **+ Add** again; add *Clean* once more. Double-click
   to name the cues (*Verse*, *Solo*, *Outro*). Right-click a cue to change its
   patch.
3. **Step through**: press **Space** (or →/↓) for the next cue, ←/↑ for the
   previous one, or click a cue. The cue and patch show above the chain.
4. **Gapless**: hold a chord and switch. There should be no gap and no click.
   Hammer Space as fast as you can while playing: still no clicks.
5. **Tails**: play a phrase on *Clean* (with the delay) and switch to *Lead*
   mid-repeat: the repeats keep ringing out under the lead sound. The status line
   shows "ringing out 1" until they've faded.
6. **Switch feel**, per patch (right-click it in *Patches*):
   - *Fade in when switched to*: how quickly it comes in (default 20 ms).
   - *Amp ring-out when switched away*: how long it keeps getting your guitar
     while fading, so its amp and cab overlap the next patch (default 150 ms;
     longer for smooth, shorter for snappy changes).
   - *Then let ring out*: *Delay and reverb ring out* (default, lighter on CPU)
     or *The whole patch rings out* (amp and cab too, until silent).
7. **Same patch twice**: make two cues in a row with the same patch. Right-click
   the second → *If the previous cue uses the same patch* → *Reset to its saved
   settings*: tweak a knob on the first, step on, and the knob snaps back. With
   *Keep playing as it is* nothing changes.
8. **Preloading**: opening a show builds every patch in the background (the
   chain title says *loading patches...*); after that, switching never loads
   anything. Watch *memory* in the status line with a big show.
9. **CPU while switching**: patches ringing out run alongside the new one, so CPU
   rises briefly after a switch. Check overruns stay 0 at your buffer size
   while stepping quickly.

## Testing milestone 4 with your guitar

Encore now opens a **show**. On first start it creates *My Show* in
`Documents\Encore\Shows` with one patch, *Clean*, using the amp and cab you
loaded last.

1. **Edit view**: patches on the left; the selected patch's chain as tiles;
   the selected block's controls below. Click a tile to edit it.
2. **Build two patches**: click **+ New** (it starts from your current amp and
   cab) and name it *Lead* (double-click to rename). Give it a different amp or
   more gain. Click between *Clean* and *Lead* to hear both. (Switching isn't
   gapless yet: that's milestone 5.)
3. **Edit the chain**: the **+** tile adds a block at the end. Right-click a
   tile to move it, insert before/after, or remove it.
4. **Saving**: there's nothing to remember. Changes autosave 2 s after you
   stop editing (top bar: *Unsaved changes* → *Saved 14:02:11*). **Ctrl+S**
   saves immediately. Closing Encore saves too.
5. **Reopen**: close and start Encore. The same show, patches and settings come
   back.
6. **Portable**: *Show → Save a copy to...* and pick a USB stick (or any
   folder). Open that copy with *Show → Open show...*: models and IRs are
   inside it, so it works on any computer even if the originals are deleted.
7. **Backups**: *Show → Open backups folder*. Every explicit save, and the
   first autosave every 5 minutes, keeps the previous `show.json` (the newest
   50 are kept). To restore one, copy it over `show.json` while Encore is closed.
8. **More shows**: *Show → New show...* creates another (e.g. one per band or
   production); *Show → Open show...* switches between them.

## Testing milestone 3 with your guitar

You need a cabinet impulse response (`.wav`). Free ones are easy to find
(e.g. on TONE3000, or from IR makers' free packs). The default chain is now
**Amp → Cab → Gate → EQ**.

1. **Load**: in the *Cab* panel click **Load cab IR...**. With an amp-only
   capture loaded, the harsh fizz should turn into a normal guitar tone. The
   panel shows the IR's sample rate and length. Encore reloads it next start.
2. **Swap while playing**: hold a chord and load another IR. It crossfades in
   about 50 ms with no gap or click. Any sample rate works; it's converted
   automatically when it loads.
3. **Latency**: the latency line must not change when you load an IR. The
   cab adds zero latency.
4. **Tone**: **Low cut** (around 80 Hz) tightens the low end, **High cut**
   (around 6–8 kHz) removes fizz. **Mix** blends the cab with the raw amp,
   **Level** makes up volume. IRs are level-normalised when loaded (NAM's
   convention), so most land at similar volumes.
5. **CPU**: the cab's CPU % should be small next to the amp's. IRs longer than
   1 s are shortened (the panel says so); cab IRs are usually 20–500 ms.
6. **Full-rig captures** (amp + cab already in the capture): switch the *Cab*
   block **OFF**.

## Testing milestone 2 with your guitar

You need a few `.nam` captures, e.g. from [TONE3000](https://www.tone3000.com)
(free; pick *NAM* captures of an amp, not "full rig" ones if you'll add a cab IR
in milestone 3). Most are amp-only, so they'll sound harsh until the cab block
arrives. That's expected.

1. **Load**: in the *Amp* panel click **Load amp...** and pick a `.nam` file.
   The panel shows its name, its sample rate, and how much CPU it measured on
   your computer. It fades in when ready; Encore reloads it at the next start.
2. **Swap while playing**: hold a chord and load a different capture. It
   should crossfade in 30 ms: no gap, no click, no dropout.
3. **Gain staging**: set **Input** so the amp breaks up the way you like.
   **Level match** (on by default) brings captures to a similar loudness if the
   file records it; the panel says "no level data" when it doesn't.
4. **Tone**: **Bass/Mid/Treble** after the amp. Sweep them while playing; no
   zipper noise.
5. **Gate after the amp**: the gate now sits after the amp with **Detect
   guitar** on, so it hears your clean guitar but cuts the amp's hiss. Turn up a
   high-gain capture, stop playing, raise **Threshold** until the hiss stops.
   Then try **Detect guitar** off to hear the difference.
6. **Sample rates**: if your interface runs at 44.1 kHz, captures (usually
   48 kHz) are converted on the fly. The panel says "(converted)" and the
   latency line shows "+ chain 0.9 ms" or so. Run your interface at 48 kHz to
   avoid that.
7. **CPU**: watch the amp's CPU % and overall CPU at 64 and 128 samples.
   Standard captures are the heaviest; "lite"/"feather" ones are lighter.
   Overruns must stay at 0.
8. **Debug build**: `RT violations` must stay 0 while loading and swapping
   amps. (NAM is optimised even in Debug builds, so you can play there.)

## Testing milestone 1 with your guitar

Set up audio as in milestone 0 below, then:

1. **Gate**: with your guitar volume up and not playing, raise **Threshold**
   until the hiss disappears and the *open* light goes off. Play: the light comes
   on instantly and notes ring out without being chopped. Lower **Range** toward
   −20 dB for a gentler gate.
2. **EQ**: sweep every knob quickly while you play. You should hear the tone
   change, but never a click, zipper noise or crackle. Double-click a knob to
   reset it.
3. **On/off**: hit each block's **ON/OFF** switch repeatedly while a chord
   rings. No clicks. An *OFF* block shows no CPU because it isn't processed at all.
4. **Limiter**: turn **Master volume** to +12 dB and dig in. The health line
   shows the limiter working (e.g. `limiter -4.5 dB`) and the output meter never
   reaches the red clip light. At normal levels it reads `idle`.
5. **Debug build only**: the health line shows `RT violations`. It must stay
   at **0** whatever you do. Anything else means the audio thread allocated
   memory, which is a bug to report.

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
  app/                the main window, per-machine settings (%APPDATA%\Encore)
  model/              the show on disk: data model, JSON format, show folders
  session/            the open show: building patches into chains, editing, autosave
  engine/             everything that runs on or talks to the audio thread
    core/             Block, Chain, PatchPlayer (switching), Parameter, handovers, lock-free queues
    blocks/           the built-in blocks (amp, cab, gate, EQ, delay)
    dsp/              allocation-free DSP (filters, limiter, sample-rate conversion)
    nam/              loading and running Neural Amp Modeler captures
  ui/                 edit view pieces: patch list, chain strip, block panel, meters
  platform/           Windows-specific tuning
Tests/                engine unit tests (run in CI)
docs/DECISIONS.md     short log of important design decisions
```

## Licence

Encore is free software under the **GNU Affero General Public License v3**
(see [LICENSE.md](LICENSE.md)). It uses JUCE under its AGPLv3 option, the
Steinberg ASIO SDK headers bundled with JUCE (GPLv3 option, compatible with
AGPLv3), [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
(MIT), [Eigen](https://eigen.tuxfamily.org) (MPL 2.0) and
[nlohmann/json](https://github.com/nlohmann/json) (MIT). All are downloaded at
configure time at pinned versions.

ASIO is a trademark of Steinberg Media Technologies GmbH.
