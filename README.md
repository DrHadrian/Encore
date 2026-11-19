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

Milestone 8: theatre cue lists (acts and scenes, automatic numbering, cut
cues, bar marks and notes, fast typing, go to a cue by number); crash
recovery, interface reconnect, pre-show check; a stage view you lay out
yourself, tuner, panic and lock; foot controllers and expression pedals over
MIDI; gapless patch switching with tails; NAM amps, cab IRs, gate, EQ, delay.
The Logic-style look (design v3): a native JUCE UI drawn from design tokens,
with an Edit view (running order, signal chain, block stage, inspector),
the full cue list, a Stage view and its layout editor, one settings window
(Audio | MIDI) and the pre-show check (see [docs/design](docs/design)).

| Milestone | What you can test |
| --- | --- |
| **0 – Toolchain and passthrough** ✅ | Dry guitar through ASIO, latency display, meters, CPU/dropout counters, master volume and mute |
| **1 – Real-time core** ✅ | Gate and EQ blocks, click-free toggles and sweeps, safety limiter |
| **2 – NAM amp block** ✅ | Play through .nam captures, swap amps while playing |
| **3 – Cab IR block** ✅ | Amp + cab, swap IRs while playing |
| **4 – Data model and saving** ✅ | Patches and shows saved to a self-contained folder, edit view |
| **5 – Patch switching** ✅ | Gapless switches with tail spillover |
| **6 – MIDI** ✅ | Foot controller, expression pedals, MIDI out |
| **7 – Performance view and tuner** ✅ | Stage view with layout editor, tuner, lock, panic |
| **8 – Theatre cue lists** | Acts/scenes/cues, insert, renumber, cut, go to |
| **9 – Robustness** ✅ | Crash recovery, reconnect, pre-show check, soak test |
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

## Testing milestone 8 with your guitar

One running order, named for the kind of show: **Set list** with *songs*
(band, worship, solo) or **Cue list** with *cues* (musical). Show → *Kind of
show* switches it; nothing else changes.

1. **The list**: the sidebar shows it compactly, with acts/scenes/sets as
   headings. **Full list** (or **F6**) opens it full size: number, name, bar
   mark, note, patch, MIDI, and cut items. **Back to patches** (or F6) closes it.
2. **Type a show in**: in the full list, select a row and press **Insert**
   (or Ctrl+Enter, or **+ Cue/+ Song**). Type the name, **Tab** to the bar
   mark, **Tab** to the note, **Enter**: the next one is created, ready for
   its name. **Enter** on an empty name stops. New items play the patch of
   the one before; click the patch cell to change it.
3. **Sections**: **+ Section** (Ctrl+Shift+Enter) adds an act/scene/set after
   the selection; right-click it for *Kind*. Items move in and out of
   sections with **Ctrl+Up/Down** (or Up/Down in the sidebar): at a
   section's edge an item steps out, next to a section it steps in.
4. **Numbers are automatic**: insert between 4 and 5 and everything after
   renumbers. **Cut** an item: it stays in the full list (struck through),
   the show skips it, the numbers close up. **Restore** brings it back.
5. **Go to**: anywhere, type the number and press **Enter** (e.g. `4 2 Enter`).
   Esc cancels. In the full list, **G** or double-clicking the number plays
   the selected item.
6. **Rehearsal**: delete (asks first; a section's items stay), move, cut and
   insert while playing: the playing item stays playing and *next* continues
   from it.
7. **Stage view**: the current cue/song shows its act/scene/set, and the set
   list widget shows the headings.

## Testing milestone 9 with your guitar

1. **Pre-show check**: Show → *Pre-show check...* or **F9**. Every line is
   green, amber or red, worst first, with a button where there's a fix
   (audio/MIDI settings, save, reset counters, unmute). It re-checks every
   second: unplug the foot controller and watch its line turn red, plug it
   back in and it turns green.
2. **Unplug the interface** while playing: within half a second the stage
   view says *AUDIO INTERFACE LOST*, the edit view says so in red. Step a cue
   or two with the footswitch. Plug it back in: Encore reopens it by itself
   with the same settings, and the steps you made count.
3. **Crash recovery**: with Encore live in the stage view on, say, cue 3,
   end it in Task Manager (*End task*). Start it again: same show, cue 3,
   stage view, live, and a note along the top. Do it three times within ten
   minutes and it comes back **muted** instead (something is wrong then).
4. **Logs**: Show → *Open log folder*. One log per run (devices, reconnects,
   recoveries); a crash also leaves *crash-<time>.txt* with details for me.
5. **Soak test** (optional, no guitar): set `ENCORE_SOAK_SECONDS=14400` and
   run `EncoreTests` for a 4-hour run of random cues, stomps, pedals, panic
   and edits. CI runs 10 minutes on every change.

## Testing milestone 7 with your guitar

1. **Stage view**: press **F5** (or **Stage** in the top bar). The default
   layout shows the cue big, the next cue, the patch, the set list, stomps,
   pedals, meters, clock, system health, lock and panic. Step with the
   footswitch or Space: everything follows. **F11** toggles full screen;
   **Edit view** (top right) or F5 goes back.
2. **Tuner**: press **T**, click **Tuner**, or map a footswitch to *Tuner*
   in **MIDI...**. Without a tuner widget in the layout it covers the screen.
   Play each string: the note turns green within about 2 cents. The output is
   muted while tuning (Show → *Stage settings* → *Tuner mutes the output* to
   change that; *Concert pitch* for A = 442 etc.). Close with T, Esc or a click.
3. **Panic**: click **PANIC** (top bar or the stage widget) or map a footswitch.
   With a long delay ringing: the sound stops within 5 ms and the repeats are
   gone. By default it comes back by itself after about a second, on the same
   cue. Show → *Stage settings* → *Panic* → *Silence until panic is pressed
   again* makes it stay silent until the next press.
4. **Lock**: click **Lock** (top right) or the lock widget, or Ctrl+L. Clicks
   and keys do nothing now (except panic); the footswitch still steps cues.
   Hold the lock for a second to unlock.
5. **Edit the layout**: **Edit layout** (top right). Drag widgets to move them,
   drag their right or bottom edge (or the corner) to resize; they snap to a
   24 x 16 grid. Right-click: text size, duplicate, bring to front, remove.
   **+ Widget** or double-click empty space adds one (try *Text* for a
   reminder, *Tuner* for a tuner that's always visible, big *Next* and *Back*
   buttons for a touch screen). Changes save with the show.
6. **Several layouts**: in edit mode, **Layout: ...** → *New layout (copy of
   this one)*, e.g. *Big monitor*. *Use as the show's default*, or step to a
   cue and choose *Use for cue ... only*: the screen switches layout when that
   cue comes up.
7. **Laptop and small screens**: resize the window; widgets scale with it.

## Testing milestone 6 with your guitar

A USB MIDI keyboard works as a stand-in foot controller: its sustain pedal is a
footswitch and its mod wheel an expression pedal.

1. **Connect**: plug in the keyboard, click **MIDI...** and tick it under
   *Listen to*. Press keys: *Last received* shows each message. Encore remembers
   the device and reopens it when you plug it back in.
2. **Next cue**: the default mappings are *sustain pedal (CC 64) = next cue*,
   *CC 11 = expression 1*, *mod wheel (CC 1) = expression 2* and *program
   change = recall*. Press the sustain pedal: the set list steps on, once per
   press, however long you hold it. This works with Encore in the background
   and while a menu is open (MIDI is handled on the audio thread).
3. **MIDI learn**: **+ Add mapping** → *Previous cue*, then press a key (or a
   footswitch): it's learned. Click **Learn** on any row to change its control,
   click the action to change what it does, **x** to remove it.
4. **Stomps**: right-click the delay tile → *Stomp switch* → *Stomp 1* (the tile
   shows STOMP 1). Map a key to *Stomp switch* → *Stomp 1*. Pressing it toggles
   the delay without clicks; another patch can put *Stomp 1* on something else.
5. **Expression**: right-click the amp's *Gain* knob (or any knob) →
   *Expression pedal* → *Expression 2*. The label shows EXP 2. Move the mod
   wheel: the knob follows (heel = minimum, toe = maximum). Switch patches with
   the wheel half-way: the new patch picks the position up at once. The knob's
   saved value is kept when the pedal moves it.
6. **Program change recall**: right-click a cue → *Recall with a program
   change...* → e.g. 5. A program change 5 from the controller jumps straight
   to that cue (the cue shows *PC 5*).
7. **MIDI out**: choose an output in **MIDI...** (or none), then right-click a
   cue → *MIDI out when this cue starts...*: e.g. `PC 1 5` or `CC 1 20 127`
   (one per line). They are sent whenever that cue starts, by footswitch,
   keyboard or click. With a synth or a MIDI monitor on that output you can see
   them arrive.
8. **Mute**: map a key to *Mute*: the **MUTED/LIVE** button follows it.
9. **Per show or per computer**: mappings belong to this computer. Tick *This
   show has its own mappings* to make the show folder carry them (it starts
   from a copy of this computer's).
10. **Hammer it**: step through cues as fast as the pedal goes while playing:
    no clicks, overruns stay 0.

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
  session/            the open show: building patches into chains, editing, autosave, pre-show check, crash recovery
  engine/             everything that runs on or talks to the audio thread
    core/             Block, Chain, PatchPlayer (switching), Parameter, handovers, lock-free queues
    blocks/           the built-in blocks (amp, cab, gate, EQ, delay)
    dsp/              allocation-free DSP (filters, limiter, sample-rate conversion)
    nam/              loading and running Neural Amp Modeler captures
    midi/             foot controller and pedal input, MIDI out
  ui/                 the screens: edit view pieces, full cue list, stage view and layout editor,
                      settings window (audio, MIDI), pre-show check, tuner display
    theme/            design v3 tokens (EncoreTheme.h), fonts, icons, the LookAndFeel
    kit/              shared components: panes, toolbar, LCD, meters, dialogs, gear placeholders
  platform/           Windows-specific tuning
Resources/ui/         bundled Inter font, icons, the wordmark
Tests/                engine and UI tests (run in CI); UI snapshots with ENCORE_UI_SNAPSHOTS=<folder>
docs/design/          design v3: design system, screens, reference boards and targets
docs/DECISIONS.md     short log of important design decisions
```

## Licence

Encore is free software under the **GNU Affero General Public License v3**
(see [LICENSE.md](LICENSE.md)). It uses JUCE under its AGPLv3 option, the
Steinberg ASIO SDK headers bundled with JUCE (GPLv3 option, compatible with
AGPLv3), [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
(MIT), [Eigen](https://eigen.tuxfamily.org) (MPL 2.0) and
[nlohmann/json](https://github.com/nlohmann/json) (MIT). All are downloaded at
configure time at pinned versions. The UI font, [Inter](https://rsms.me/inter/)
by Rasmus Andersson, is bundled in `Resources/ui/fonts` under the SIL Open
Font License 1.1 (`OFL-and-copyright.txt` there).

ASIO is a trademark of Steinberg Media Technologies GmbH.
