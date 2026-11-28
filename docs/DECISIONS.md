# Design decisions

Short records of choices that shape the code, newest last. Each says what was
decided and why, so later changes can be weighed against the original reason.

## 0001 — JUCE 9, pinned, fetched by CMake (milestone 0)

JUCE 9.0.5 is downloaded by `FetchContent` at an exact tag instead of being a git
submodule. One `cmake --preset` command gets a working build with no extra git
steps, and the version is pinned in one place (`ENCORE_JUCE_VERSION`).
`ENCORE_JUCE_DIR` points at a local checkout for offline work. JUCE 9 also ships
the ASIO header, so no separate ASIO SDK download is needed.

## 0002 — The audio thread only reads atomics (milestone 0)

The UI talks to the engine only through `std::atomic` values (gain, mute, input
channel, meters, counters). The audio callback never allocates, locks, logs or
touches files. Later milestones extend this with lock-free FIFOs for events and
prebuilt chains, and a "graveyard" FIFO so objects are never freed on the audio
thread.

## 0003 — Every gain change is ramped (milestone 0)

Master volume and mute go through a 20 ms linear ramp (`juce::SmoothedValue`).
An instant gain jump produces a click; 20 ms is too short to hear as a fade.
When a device (re)starts, the ramp starts from silence.

## 0004 — Start muted, every time (milestone 0)

Encore opens muted, whatever state it closed in. A laptop booting next to a live
PA must never blast. Crash recovery (milestone 9) follows the same rule: it
reopens on the right cue, but muted until confirmed.

## 0005 — Hard clamp until the limiter exists (milestone 0)

Output is clamped just under 0 dBFS so a feedback squeal or a hot setting can't
send full-scale garbage to the desk. It counts each clipped block so the UI can
show it. Milestone 1 put a safety limiter in front of it (see 0011); the clamp stays as
a last resort that should never act.

## 0006 — Machine settings stay out of shows (milestone 0)

Device, buffer, input and volume live in `%APPDATA%\Encore\Encore.settings`.
Show folders (milestone 4) hold only what belongs to the show, so the same show
runs on any computer.

## 0007 — One instance at a time (milestone 0)

Most ASIO drivers handle two clients badly, so a second launch just brings the
running Encore to the front.

## 0008 — Chains are owned by the message thread (milestone 1)

A chain is built and prepared on the message thread, which keeps owning it.
The audio thread receives only a raw pointer through a lock-free queue, and
hands the previous chain back through a second queue (the "graveyard") when it
switches. The message thread deletes it from there. So nothing is ever created
or destroyed on the audio thread, and the UI can safely point at blocks of any
chain it owns. To change a chain, build a new one and hand it over; a playing
chain's block list never changes. Milestone 5 adds crossfades and tail
spill-over on top of the same handover.

## 0009 — Bypass is a 10 ms crossfade, and "off" means asleep (milestone 1)

Switching a block off fades between processed and dry signal over 10 ms, so
stomp toggles never click. Once the fade finishes, the block is skipped
completely and costs no CPU. Waking up resets its state first, so a filter
doesn't replay stale memory.

## 0010 — Our own biquads instead of juce::dsp::IIR (milestone 1)

`juce::dsp::IIR::Coefficients` are reference-counted heap objects, so
recalculating them while a knob moves would allocate on the audio thread.
`dsp::BiquadCoefficients` is a plain struct (RBJ cookbook formulas), computed
every 32 samples while a parameter ramps and never otherwise. Filter state is
double precision so low corners stay stable.

## 0011 — Zero-latency safety limiter (milestone 1)

The master section ends in a brickwall limiter at −1 dBFS with instant attack
and 150 ms release. It has no look-ahead, so it adds nothing to the round-trip
latency. It distorts briefly when it acts, which is the right trade for a net
that should only catch accidents. The UI shows when it works, so a patch that
leans on it gets noticed and fixed.

## 0012 — Real-time violations are detected, not hoped away (milestone 1)

Debug builds on Windows install a CRT allocation hook. Any allocation or free
inside the audio callback is counted and breaks into the debugger. The test
executable replaces global `operator new/delete` to do the same on any
platform, and a test drives gate and EQ through sweeps, bypass toggles and a
chain swap while asserting zero allocations.

## 0013 — NAM built from source, as an object library (milestone 2)

NeuralAmpModelerCore v0.6.0 and the exact Eigen commit it pins are fetched as
sources only, and `nam_core` compiles them directly. It is an OBJECT library
because NAM registers each architecture (WaveNet, LSTM...) from static
initialisers, which a linker silently drops from a static library: models
would fail with "No config parser registered". NAM processes floats directly
(`NAM_SAMPLE_FLOAT`), so no conversion buffers are needed.

NAM is compiled optimised even in Debug builds, with Eigen's internal asserts
off: an unoptimised network can't keep up with real time, and playing through
a Debug build is how the allocation tripwire gets exercised. MSVC's `/RTC1`
run-time checks are incompatible with optimisation, so they are off
project-wide; the tripwire and the tests cover what matters for real-time code.

## 0014 — Captures run at their own sample rate (milestone 2)

A capture trained at 48 kHz played at 44.1 kHz is a different (wrong) amp, so
models that can't adapt run inside a `RateBridge`: windowed-sinc conversion to
the model's rate and back (32 taps, Kaiser window, cutoff lowered when going
down, so nothing aliases). Each call returns exactly as many samples as it got,
with a fixed delay. The delay is measured at prepare time, not estimated, and
reported as block latency (≈ 0.9 ms at 44.1 kHz). The UI says when a model is
converted, so running the interface at 48 kHz avoids it.

## 0015 — Amp changes crossfade on the audio thread (milestone 2)

A model is loaded, built, prewarmed and benchmarked on a background thread,
installed on the message thread, and handed to the audio thread through the
same handover as chains (now a generic `RealtimeHandover<T>`). The audio thread
runs old and new model side by side for 30 ms and crossfades linearly. Two amps
fed the same guitar are strongly correlated, so a linear fade keeps the level
steady where equal-power would bump it by up to 3 dB. Each block owns its own
model instances, because networks have internal state.

## 0016 — The gate listens to the clean guitar by default (milestone 2)

Every chain keeps a copy of its dry input and passes it to blocks in a
`ProcessContext`. The gate's "Detect guitar" option uses it as the detector
while cutting the signal where the gate sits. The default chain places the gate
after the amp with this on: it removes the amp's amplified hiss using the clean
signal's clear note endings, the classic "gate after the amp, keyed from the
guitar" setup.

## 0017 — Tell Windows Encore is a live audio app (after milestone 2)

Testing showed overruns at 32 samples as soon as Encore lost focus on battery:
Windows' power throttling ("efficiency mode") slows background apps' threads
and timers. Encore now does what DAWs do:
- opts its process out of power throttling (execution speed and timer
  resolution);
- runs at High process priority (not Realtime, which can starve input
  handling and the driver itself);
- registers its audio thread with MMCSS "Pro Audio" at critical priority, on
  the first callback per thread (one system call, no C++ heap; harmless if the
  driver already did it);
- keeps the system and display awake while running.
The status line shows what took effect, and warns when running on battery.

## 0018 — Cab IRs: JUCE's convolution, fed from the audio thread (milestone 3)

`juce::dsp::Convolution` gives zero latency (uniform partitioned), resamples
the IR to the device rate on its own background thread, and crossfades to a new
IR over 50 ms. Its load call is wait-free and, given a moved buffer, doesn't
allocate. JUCE asks that loads aren't interleaved with processing from another
thread, so Encore reads the file on a background thread (mono mix-down, capped
at 1 s) and hands the buffer to the audio thread through a
`RealtimeHandover`. The audio thread passes it to the engine between blocks,
and the emptied holder goes back for deletion. A test swaps IRs while
processing and asserts zero audio-thread allocations. All cab blocks share one
convolution loader thread, because patches will preload many of them.

IRs are normalised to unit energy at −18 dB (JUCE's option, which matches
NAM's IR loader), so IRs from different makers land at similar volumes; a
Level knob trims the rest.

## 0019 — The show model is plain values; the live chain is built from it (milestone 4)

`ShowModel` (patches, lanes, blocks, and a tree of sections and cues) is plain
data with value semantics: easy to copy, compare and test, with no audio
objects inside. Selecting a patch copies it to a background thread, where
`buildChain()` creates the blocks and loads every amp model and cab IR
*before* the chain ever plays. Models are installed directly instead of faded
in, and IRs are fully prepared. Then the finished chain goes through the
handover. Knob changes go straight to the live blocks (atomics, as before),
and `captureChain()` copies them back into the model when saving, switching
patch or editing the chain structure. A build generation counter drops results
that a newer build has overtaken.

## 0020 — Saving: self-contained folders, atomic writes, autosave (milestone 4)

A show is a folder (`Name.encore/`) with `show.json` plus copies of every
model and IR it uses (`models/`, `irs/`), stored as show-relative paths.
Files picked from elsewhere are copied in on the next save: identical files are
shared, and different files with the same name get numbered names. The folder
can be copied anywhere. A file that is missing when a patch loads keeps its
path in the show (the block just passes the signal through), so a missing USB
stick never deletes references.

`show.json` is written to a hidden temporary file, flushed, and swapped in. A
crash mid-save leaves the old or the new file, never a broken one. Edits
autosave 2 s after the last change. The previous version goes to `backups/` on
every explicit save and at most every 5 minutes otherwise, and the newest 50
are kept. Closing Encore saves unsaved edits. The JSON carries a schema
version: older files will be migrated, and files from a newer Encore are refused
with a clear message rather than half-loaded.

## 0021 — Switching happens on the audio thread, between preloaded chains (milestone 5)

Opening a show builds a chain for every patch (the first to play first). The
engine's `PatchPlayer` owns them. The running order is published as an
immutable table of cues pointing at chains, so "next" is an array index on the
audio thread: no search, no allocation, no loading. Commands (next, previous,
go to cue, play patch) go through a lock-free queue and take effect at the
start of the next buffer. A busy screen never delays a switch, and MIDI
(milestone 6) uses the same path. A cue whose patch isn't available keeps its
place (cue numbers always match the screen) and simply doesn't change the sound.

## 0022 — Crossfade the inputs, not the outputs (milestone 5)

For 10 ms the old patch's input fades out while the new patch's fades in, and
both outputs are summed. With two identical patches the result is exactly the
input (a test checks this to 0.00001), so there is no gap and no click by
construction. Each patch's own decay (amp, cab, delay) carries the sound
across naturally, instead of an artificial output fade cutting it.

## 0023 — Tails: ring out until silent, cheaply (milestone 5)

The old patch keeps running on silence until it falls below −90 dBFS for
100 ms (8 s at most). In "tails only" mode (the default), after 200 ms of
settling it runs only from its first delay/reverb block on, so the amp and
cab stop costing CPU. A patch that played for under 50 ms (stepping quickly
through cues) doesn't ring out at all. Up to three patches can ring at once; a
fourth stops the oldest, which is the quietest. Going back to a patch that's
still ringing picks it up as it is, tail included, instead of starting a second
copy.

## 0024 — Freeing a patch only when the audio thread can't reach it (milestone 5)

Removing or rebuilding a patch releases its old chain, but deletion waits until
(a) the audio thread uses a running-order table published after the release
(which can't contain it), and (b) the chain isn't playing or ringing out (the
audio thread publishes those slots as atomics). Commands carry chain pointers,
but the audio thread only acts on ones found in its current table (comparing
pointers, never dereferencing), so a stale command is simply ignored.

## 0025 — Keyboard focus belongs to the show (milestone 5)

In the edit view, buttons, knobs and menus don't take keyboard focus. Space
and the arrow keys therefore always step the set list, rather than nudging the
last knob you touched or, worse, changing the guitar input selected in a combo
box. The mouse works as usual. The performance view (milestone 7) adds a lock.

## 0026 — Switch fades are per patch, and asymmetric by default (after milestone 5)

Testing showed equal 10 ms fades made the leaving amp and cab stop too
abruptly. Each patch now has a fade-in time (20 ms by default: how quickly it
comes in) and an amp ring-out time (150 ms by default: how long it keeps
getting the guitar while fading out). For those 150 ms the old and new amps
overlap, like letting a chord ring into the next sound, instead of the old
one being cut. With equal times the crossfade is still mathematically
seamless (the test sets both to 10 ms). With the defaults the level can rise
briefly during the overlap; that's the intended "ring", not a click (a test
checks the output never jumps faster than a smooth sine at its peak).

## 0027 — MIDI is handled on the audio thread (milestone 6)

MIDI drivers call Encore on their own threads (several at once with several
devices). Those threads only push the 3-byte message into a lock-free queue
(they take turns among themselves on a spin lock; the audio thread never waits
on it). At the start of each audio buffer the audio thread reads the queue and
acts at once: next cue, stomp, expression, mute. A footswitch therefore works
within one buffer (under 2 ms at 64 samples), whatever the screen is doing,
and even with Encore in the background or a menu open. Mappings reach the audio
thread as immutable tables, like the running order.

## 0028 — Physical controls → named controls → per-patch assignments (milestone 6)

Mappings turn MIDI into named controls: *Next cue*, *Stomp 3*, *Expression 1*.
Mappings belong to the computer (the controller is plugged into it), unless a
show asks to carry its own. Each patch then says which blocks *Stomp 3*
switches and which knobs *Expression 1* moves (with a min and max). So changing
the controller means relearning a few mappings, not editing every patch, and a
show folder still works on another computer with another controller.

## 0029 — Footswitches act once per press (milestone 6)

A CC counts as a press when it goes from below 64 to 64 or more; a note-on or
a program change is always a press. Holding a switch or a controller resending
its value never steps twice; latching and momentary switches both work. While
MIDI learn is listening, messages are shown but trigger nothing, so learning
the next-cue switch doesn't also step the show.

## 0030 — Pedals keep their position across patches (milestone 6)

The last value of each expression pedal is remembered. A patch switched to
gets the pedal's current position straight away, so a volume or wah pedal
doesn't jump when the next sound comes in. A pedal writes the live value only:
the knob's saved value stays what you set, and capturing a patch never saves
wherever the pedal happened to be.

## 0031 — MIDI out from its own thread (milestone 6)

Sending MIDI can block in the driver, so the audio thread only records which
cue started (a lock-free queue), and a background thread sends that cue's
messages to the chosen output. Output devices that disappear are closed, and
reopened when they return (checked every 2 s).

## 0032 — The tuner listens on the audio thread, thinks on the UI thread (milestone 7)

While the tuner is open, the audio thread low-passes and decimates the raw
input to about 12 kHz into a ring of atomics: a few operations per sample, no
locks. The screen reads the newest ~110 ms 20-30 times a second and runs YIN
pitch detection there. YIN copes with the strong second harmonic of low
strings (it doesn't jump an octave). High notes are measured over several
periods at once, so they read within a cent too. Closed, the tuner costs
nothing.

## 0033 — Panic fades first, then clears (milestone 7)

Panic ramps the output to silence in 5 ms (a click-free cut), and only then,
with nothing audible, drops every ringing patch and clears the playing
patch's delay lines and filters. Then, by the show's setting, it either
comes back by itself (0.3 s silent, 0.5 s fade in, same cue) or waits for a
second press. Panic works even when the screen is locked: it's the emergency
stop.

## 0034 — Stage layouts are widgets on a 24 x 16 grid (milestone 7)

The stage view is entirely made of widgets the user places, so one show can
work on a laptop, a big monitor or a touch screen. Positions are grid cells,
not pixels, so a layout scales to any window. Layouts belong to the show
(named, one default, any cue can pick another), because what you need to
see differs between gigs, not between computers. Shows without layouts get
the built-in one, which becomes the show's own as soon as it's edited.
Unknown widget types from newer versions are skipped when loading.

## 0035 — Lock blocks the screen, not the feet (milestone 7)

Locked, mouse clicks and keys change nothing (no accidental cue jumps from a
bumped laptop or a cable on the trackpad); footswitches, pedals and panic
keep working. Unlocking takes a deliberate one-second hold.

## 0036 — Invalid audio never reaches the speakers (milestone 9)

Every patch's output is checked each buffer (one pass summing magnitudes:
NaN or inf makes the sum non-finite). A bad buffer is replaced by silence and
that patch's state is reset (an unstable filter or a misbehaving block
recovers by itself). The driver's input is checked the same way, and the
final mix once more before the limiter, whose smoothing a NaN would poison
for good. Counters show in the pre-show check.

## 0037 — Crash recovery from a small state file (milestone 9)

While running, Encore keeps recovery.json (show, cue, live/muted, view) up to
date, written atomically only when something changed. A clean exit deletes it,
so finding it at start-up means a crash, a power cut or a kill: the show is
reopened at that cue, in that view, and live if it was live. If that happens
a third time within ten minutes, it comes back muted: a crash loop needs a
person to look. The audio thread is never involved.

## 0038 — The audio watchdog reopens the same interface (milestone 9)

The engine counts its callbacks. If the count stops for 500 ms while a device
is open (a stalled ASIO driver, a pulled USB cable) or the device closes, the
UI closes it and retries the last working setup (type, device, rate, buffer)
once a second, as soon as the device is listed again. Cue steps from the
keyboard or footswitch wait in their queues and apply when audio is back.
Nothing happens while the audio settings dialog is open (the user may be
changing devices on purpose).

## 0039 — Soak test in CI (milestone 9)

A test plays random notes through four patches (two NAM amps, delays, a
full-chain tail patch) while randomly stepping cues, toggling stomps, moving
pedals, panicking, opening the tuner, rebuilding patches and injecting
invalid input, checking every buffer: finite, never above full scale, no
audio-thread allocations, ringing bounded, every replaced patch freed. CI
runs 10 minutes of audio per change; ENCORE_SOAK_SECONDS sets longer runs.

## 0040 — Cue numbers are always automatic (milestone 8)

Cues are numbered 1..N in playing order after every change; inserting a cue
renumbers everything after it, and cut cues have no number. (Chosen over
point cues like 42.5: the numbers always match the order on screen.) Numbers
are stored for display and "go to", but never typed. Older shows are
renumbered when opened.

## 0041 — Edits address items by id; the playing cue follows its id (milestone 8)

Sections, cues and cut cues are edited by their stable ids, not positions,
so a list redrawn mid-edit can't hit the wrong row. After every structural
change the running order is republished and the player's position is set
to wherever the playing cue's id now is, so *next* continues from the cue
that's actually sounding. Moving a cue past a section's edge steps it out of
the section; moving it next to a section steps it in. Deleting a section
keeps its cues.

## 0042 — Only the newest running order waits while audio is stopped (milestone 8)

The handover queue to the audio thread fills only when the audio thread
isn't running (no device, interface unplugged). Then each new version
replaces the one waiting (which the audio thread never saw, so it can be
freed at once) and is queued as soon as there's room. Before, editing more
than eight times while the interface was unplugged could leave the show
playing an older running order after reconnecting.

## 0043 — One running order; its name follows the kind of show (milestone 8)

A set list and a cue list are the same thing: an ordered list of items that
each recall a patch, grouped in sections. Encore has one, and the show's kind
picks the words: band, worship and solo shows have a **Set list** of *songs*
(sections default to *sets*); musicals have a **Cue list** of *cues*
(sections default to *scenes*). The sidebar is its compact form (sections as
headings, cut items hidden) and expands into the full table for editing.
Keeping one model means every feature (go to, cut, MIDI per item, per-item
stage layouts) works for every kind of gig.

## 0044 — UI is native JUCE with a token-driven LookAndFeel; gear art behind GearView (design v3)

The design v3 look is built in JUCE itself: every colour, size and font comes
from `Source/ui/theme/EncoreTheme.h`, `EncoreLookAndFeel` draws the standard
controls from those tokens, and the screens are composed from a small kit
(`Source/ui/kit`: panes, toolbar, LCD, meters, dialogs, status badges). No web
view, HTML or new UI library: the UI stays one process, one thread model and
one set of tools, and costs almost nothing on stage (repaint only on change,
nothing built in `paint()`, no new timers; the existing 30 Hz timer drives
it). Amps, cabs and pedals are drawn as placeholders behind the `GearView`
interface, at the exact boxes of the future artwork, so real gear art can
replace them without touching a screen. Snapshot tests render every screen
and `docs/design/tools/ui-compare.py` compares them with the design targets.


## 0045 — Plugins are scanned, and tried, in a separate process (milestone 10)

Encore never loads an unknown plugin into itself. A scan lists the VST3 files
in the chosen folders (default: the standard VST3 folder) and starts a copy of
Encore for each new or changed file (`Encore --scan-plugin <file> <result>`).
That copy lists the plugins in the file **and loads and plays each one
briefly** (VST3 can list from `moduleinfo.json` without loading anything, which
would prove nothing). If the copy crashes, or doesn't finish within 60 s, the
file goes on a blocklist with the reason, and Encore carries on; a full scan
(or "try again") retries it. The plugin list, the blocklist and the folders
belong to the computer, not the show (0006), in one XML file in the app-data
folder. Known, unchanged files aren't scanned again. Instruments without an
audio input aren't offered.

The scanner treats plugins as a host does (after the first real-world test):
each one gets time on the message loop to finish starting and closing
(tearing one down at once made some report a crash on their next start); the
"time limit" counts from the last plugin finished, so a shell holding many
plugins isn't cut off; a plugin that loads but can't be used (no audio input
or output, failed to create) is listed under Blocked with why, never silently
dropped; main buses switched off by default are switched on; each scanned
file gets a line in plugin-scan.log next to the plugin list. Plugins are
matched to scanned files by the .vst3 folder they're in: without a
moduleinfo.json, JUCE names the DLL inside the folder (UAD's plugins
vanished at the end of every scan). Instruments are skipped without being
loaded. Plugins get a play head (stopped, 120 BPM, 4/4), as hosts provide.

## 0046 — Plugin blocks: created on the message thread, guarded while playing (milestone 10)

A plugin is a block like the built-in ones (stomps, expression pedals,
gapless switching, tails). VST3 plugins are created, set up (buses, saved
state), prepared, reset and closed on the message thread; a loader thread asks
the message thread and waits, giving up if the show closes or Encore quits.
Encore is mono until routing (milestone 11): the guitar feeds every input of
the plugin and a stereo output is averaged back to mono.

In-process crash guard (process isolation is milestone 12):
- a plugin that crashes inside its audio processing (Windows: a structured
  exception), outputs NaN/inf, or takes longer than the whole buffer three
  callbacks in a row is switched off at once; the dry signal passes and the
  fault is reported. A crashed plugin is never called again, not even to be
  closed, because it may still hold its own locks: it stays in memory until
  Encore quits. The other faults can be turned back on.
- the crash handler records which plugin the crash was in; after such a
  crash that plugin's blocks start switched off, with a notice, until turned
  back on. It's the plugin Encore was calling on that thread, or else the
  plugin whose file the crashing code (or a caller further down the stack)
  is in: plugins also crash in their own windows and threads, where Encore
  isn't calling them. The crash report names what happened (e.g. an access
  violation), the file and offset, the thread, and every frame by file,
  since plugins and a Release Encore have no symbols.
- a reset (panic, a patch finished ringing) is done on the message thread,
  since a plugin's reset may allocate. Switched off, a plugin is fed silence
  until its tail has died away, so switching it back on doesn't replay an old
  tail. Plugins always count as having a tail (many report none).

## 0047 — The plugin's own state holds its settings; shows with plugins are schema 2 (milestone 10)

A plugin block saves which plugin it is (format, name, maker, unique ID,
version and where it was installed, as a hint) and the plugin's own state
(base64). Its parameters aren't saved one by one, except knobs on an
expression pedal, which keep their own saved value like any block's. The
state is read from the plugin only when something changed (parameters,
programs, the plugin saying so), so autosave stays cheap. On a computer
without the plugin (or with it blocked) the block passes the signal through
and keeps everything it saved. A show that contains a plugin is written as
schema 2; a show without one stays schema 1, so older Encore versions keep
opening it. Adding, removing or moving a block reloads the patch's plugins
(their settings carry over).

## 0048 — Plugins in the UI: thin, from the kit, ready to be redesigned (milestone 10)

The design is still moving, so the plugin screens hold no logic: the catalog
(session) and the plugin block (engine) do everything, and each screen is a
small file built from `Source/ui/kit` and the theme tokens. Settings gets a
**Plugins** tab (the plugin list where MIDI has its mappings, folders and the
blocklist on the right). Plugins are added from the chain's Add block menu
(*Plugin* → maker → plugin); the Library panel in PROPOSALS can replace that
later on the same catalog. The block stage shows a plugin's first 8
parameters as knobs and the inspector its first 48, because plugins can have
hundreds; the rest are in the plugin's own window, a native window around the
plugin's editor. That window closes before its plugin does and comes back
when the patch is rebuilt (an edit reloads the patch's plugins); the
main window doesn't pull keyboard focus away from it (decision 0025 still
holds otherwise). Encore's own on/off replaces the plugin's bypass parameter.
Encore never asks a plugin `hasEditor()`: for VST3 that makes the plugin
build its window and throw it away (JUCE notes it crashes some plugins), and
the screens refreshing 30 times a second did exactly that until a plugin
crashed. "Open plugin window..." is on whenever the plugin is loaded; the
window is created once, and shows a parameter list if the plugin has none.

## 0049 — Build and test speed: compiler cache, precompiled JUCE, parallel tests, nightly soak

CI took about 17 minutes a run, ~14 of them compiling (MSBuild, every file
from scratch each time); the tests took ~1.5. Now:
- CI builds with **Ninja** (as the CMake presets do) and **sccache**, the
  compiler cache that works with MSVC (it needs debug info embedded in the
  objects, `/Z7`). Only what changed compiles. Debug and Release run side by side.
- Encore's own sources compile against a **precompiled header** of JUCE
  (`ENCORE_PCH`, on by default): parsing JUCE's headers is ~95 % of compiling
  a typical Encore file (measured: 5.2 s of 5.5 s). JUCE's module sources
  keep compiling as they were. CI turns it off: sccache can't reuse files
  compiled against a precompiled header, and building without it proves every
  file includes what it uses.
- The tests run as **four processes in parallel** (robustness, plugins,
  screens, the rest); together they run exactly the full suite.
- **Link-time optimisation** (`ENCORE_LTO`) stays on for the app players use;
  pull requests skip it.
- The **4-hour soak** runs every night (and on demand); every change runs a
  2-minute one. Documentation-only changes don't build.
- Not adopted (yet): C++20 modules / `import std` (still behind an
  experimental switch in CMake, and JUCE is built from headers and macros);
  CMake unity builds (JUCE's modules already are one, and they would merge
  Encore's files); another linker (MSVC's is needed with LTO, and linking
  isn't the slow step here). The app and the tests compile the shared sources
  twice on purpose (different JUCE settings); sharing a library is the next
  step if builds grow.

## 0050 — Routing belongs to the computer; shows use names (milestone 11)

Inputs and outputs are named per computer ("Guitar" = input 1, "In-ear" =
outputs 3/4) and stored with its settings (`%APPDATA%\Encore`), never in the
show, as MIDI mappings and plugins are. A show only names them, so it plays
right on another interface. Up to 8 inputs and 8 outputs; names 1–32
characters, unique ignoring case. After updating there is one input
("Guitar", the old guitar-input setting) and one output, "Main" = all
outputs, which plays exactly as before. A name a show uses that this
computer hasn't is never an error that stops anything: the patch plays the
first input, a send to a missing output goes nowhere, and the pre-show
check, the Routing view and the IN / OUT stages say so (amber, NOT HERE).
- Outputs keep their engine slot, meter, mute and delay line by name when
  the routing changes; MIDI "Mute output" bindings store the name too, so
  they follow a renamed output only if the binding is renamed.
- A routing change applies on the next buffer without a click (gains ramp,
  the EQ glides, a delay change crossfades). The interface channels the
  routing uses are switched on if the interface has them; Encore never
  switches a channel off.
- Mutes are saved: a muted output is still muted after a restart, and the
  pre-show check warns about it.

## 0051 — Sends per patch; schema 3 only when a show uses routing (milestone 11)

A patch has a send level per output name (−∞…+6 dB); an output the patch
doesn't list gets 0 dB, so a new output gets every patch until it's turned
down. Sends ramp over 20 ms on every change and every patch switch, and a
ringing tail keeps its own patch's sends. A show is written as schema 3 only
when it uses routing (an input other than the first, a send other than the
default, or a split); otherwise it stays schema 1 or 2 and older versions
still open it.

## 0052 — Stereo from where it starts; amp and cab stay mono (milestone 11)

A chain carries one or two channels: each block says what it puts out. NAM
amps and cab IRs are mono (the captures and IRs are), as are the gate, EQ
and delay given mono; a stereo plugin and the merge make the patch stereo
from there on. The guitar is mono until then, which keeps the common patch
as cheap as before. Mono into a stereo output is the same on both sides;
stereo into a mono output is (L + R) / 2.

## 0053 — Split paths: one split per patch, aligned by reported latency (milestone 11)

`split` and `merge` are blocks in the main lane; the blocks between them are
path A, a second lane is path B. One split per patch keeps the model and the
screens simple (two rows). The split's on/off is "path B off" (so a stomp or
a footswitch can drop the second amp); the merge has a level and pan per
path and is never off. Paths are lined up with the latency the blocks report
when the patch is prepared (a plugin that changes its latency later is lined
up at the next rebuild), up to 8192 samples (≈170 ms at 48 kHz); more is a
pre-show warning. Capturing a live chain finds each block's saved settings by
its id, whichever path it is on, so moving a block between paths keeps them.

## 0054 — Routing lives in Edit (F7), not in Settings (design v3, milestone 11)

As designed: routing, patches and cues are one way of working, so the
Routing view opens over Edit's centre (Routing button, F7) with *Inputs &
outputs* (this computer) and *Sends* (this show); the chain gets IN and OUT
tiles whose stages edit the playing patch's input and sends live. Settings
keeps Audio | MIDI | Plugins. The bottom row replaces the old input picker
and master meters with the playing patch's input and a strip per output (M
and meter), Compact | Full remembered on the computer; Full scrolls sideways
without a scroll bar, the cut-off strip showing there's more. Picking which
input a patch plays moved to the IN tile and the Routing view. Changes made
from a button inside the Routing view apply after its click returns (the
view rebuilds and a button can't delete itself). The pre-show check's
"Routing" line (all fine) appears only when the routing is more than the
default, so a default rig's check doesn't grow.

## 0055 — Stage Outputs widget: opt-in, obeys the lock (milestone 11)

The Outputs widget (a meter and a big Mute / MUTED per output) is added from
the layout editor; the default layout is unchanged, because most rigs have
one output. Its mutes go through the same path as the bottom row's M (kept
in this computer's routing). Locked, they do nothing: the lock guards
against accidental presses, and only Panic works through it. The Patch
widget shows the input the patch plays, amber when it isn't the patch's own.

## 0056 — M11 screens: small choices made while building (design v3)

- The IN / OUT tiles' inspector is a read-only list (kit `InfoList`) of what
  the stage shows, with "Open routing"; nothing there edits.
- An inspector lists the selected block's own assignments first, then the
  rest of the patch's.
- Output names squeeze up to 4 % before they're shortened with "...", as the
  inspector's lines do (font metrics differ a little between renderers).
- The global button padding stayed as it was: a wider one improved the
  routing boards but made the existing Edit boards worse.

## 0057 — One build of JUCE and of Encore's code for the app and the tests; the amp art as one zip

The first CI run of PR #15 took 14 minutes: its caches were cold (a new pull
request can only reuse caches from itself, its base branch and main), and the
build had grown:
- **The amp art** was 236 compile units (JUCE makes one source file per
  resource). It now goes in as **one zip**, built by CMake from the same files
  during every build (rebuilt when a layer is added or changed, never by hand,
  not stored in the repo), and `kit::ampArt` reads it with `juce::ZipFile`. The
  app stays one .exe.
- **JUCE and Encore's own code are compiled once** and linked by both programs
  (OBJECT libraries `EncoreJuce`, `EncoreEngine`, `EncoreUi`, as `nam_core`
  already was: every object is linked, so no static initialiser is dropped).
  Before, the app and the tests each compiled them, with different JUCE
  settings. Now there is **one JUCE configuration**: the tests run exactly what
  ships. It includes `JUCE_MODAL_LOOPS_PERMITTED=1`, which the tests need to
  pump the message loop; it only makes JUCE's modal-loop functions available,
  and Encore still never runs a modal loop. `JUCE_UNIT_TESTS` (JUCE's own tests)
  is gone; Encore's test count is unchanged. With `ENCORE_LTO` the shared code
  is compiled for LTO too.
- **The amp tokens** (`theme::amp`) moved to `EncoreAmpTheme.h`, included only
  by the files that draw amps: tuning an amp no longer rebuilds the whole UI.

A cold Release build on CI compiled 673 files; it now compiles roughly half.
Warm runs reuse the cache as before. Still open: new pull requests in a stack
start cold until main has the stack's code (merging the stack fixes that for
every later pull request).

## 0058 — Main is protected: changes go in through pull requests with a green "CI"

From 19 Nov 2026, main takes changes only through pull requests, and a pull
request merges only when the **CI** check is green. That keeps main always
buildable and tested, for players and for every branch started from it.
- **One required check, "CI".** It's the last job of the build workflow. It's
  green when the Windows Debug and Release builds and their tests passed, or
  when the change needed no build. A required check can't name a matrix job that
  was skipped, so this job stands for them.
- **Every pull request runs**, so the check always reports. A job named
  "Changes" looks at the pull request's files. When only docs changed (`*.md`,
  `docs/`, `LICENSE`), the Windows builds are skipped and CI is green within a
  minute. Pushes to main still skip docs-only changes (`paths-ignore`).
- **How it's set:** a repository ruleset named "main" (Settings › Rules ›
  Rulesets), active on the default branch.
  - It requires a pull request (0 approvals), the "CI" check, and merge
    commits only, which keep each step's commit.
  - It blocks deletions and force pushes.
- **No one can bypass it, admins included**: the bypass list is empty. In an
  emergency, an admin sets the ruleset's enforcement to Disabled for a moment
  (or adds themselves to the bypass list), then sets it back.
- **Main's cache serves the pull requests.** Pushes to main now build like pull
  requests, without link-time optimisation. A pull request can reuse main's
  compiler cache, but objects built for LTO can't be reused without it. Before
  this, every pull request's Release build started from nothing. The players'
  build (LTO) is made every night and when the workflow is run by hand, with
  its own cache (`windows-Release-lto`). It also tests the LTO build, which a
  pull request doesn't.
- **One compiler cache per compiler version.** GitHub rolls out new Windows
  images gradually, so for a while runs land on the old or the new MSVC at
  random. A cache filled by one compiler misses everything on the other (seen
  on #17: 0 % hits, MSVC 14.51 after 14.52). The cache key now includes the
  MSVC version, so each compiler keeps its own cache.

## 0059 — Focus rings only after the keyboard; no control takes focus from a click

Some buttons showed a blue rectangle after a click, until the next click
elsewhere. That was the keyboard-focus ring, in two situations:
- **The amp builder's buttons** (Load capture…, Duplicate, Remove) still took
  focus when clicked, against decision 0025. That also meant Space or the arrows
  would have gone to them.
- **The default button of a dialog** (Rename, Delete…) gets focus so Enter
  works, and the ring was drawn although the keyboard was never used.

Now (as DESIGN-SYSTEM already said: "only after keyboard navigation, never on click"):
- **Rings follow CSS `:focus-visible`** (`theme::focusRings`). A ring is drawn
  only when focus last moved by the keyboard. A click anywhere hides it until
  Tab or another key moves focus again. Focus itself doesn't change, so Enter
  and Space work as before. Every ring goes through `paint::focusRing`, so all
  controls follow this.
- **A test walks every control** under the main window (Edit, Routing, the
  amp builder with each kind of part selected, Stage). It fails on any button,
  slider or popup that would take keyboard focus from a click.
- **The same day, a stray mark** next to the amp builder's picker turned out to
  be its `paint()` cutting 15 px off the title area on every repaint. A test now
  repaints the toolbar 13 times and compares, and a scan of every `paint()` found
  no other place that changes state.


## 0060 — Encore never kills a thread; a hang in the tests prints its stacks

Main's CI on 4d72c76 hung in the amp builder test until ctest's 10-minute
limit, and the log named only the test. It happened about once in four full
Windows runs, always with the four test processes running at once.

**The cause.** `juce::ThreadPool` (the loader pool, `BackgroundJobs`) gives
each thread 500 ms to stop and then calls `TerminateThread`. A thread that is
ending holds the Windows loader lock while every DLL is told. On a busy runner
that can take longer than 500 ms. Killed then, the lock stays held for good:
every thread start, thread exit and DLL load after it waits forever.

What the watchdog (below) showed in the hung runs:
- no thread held the lock;
- new threads were waiting for it inside the loader;
- the test thread was waiting on a thread that couldn't end;
- the watchdog's own symbol helper, a new thread, couldn't start.

It hit the test that followed a pool's end. Another run hung at exit instead,
after its last test. The MIDI sender (1 s), the plugin scan thread (10 s) and
a test device (10 s) had the same time-limited stop. In the app this could
have hung Encore at a show change or at quit, however rarely.

**The rule:**
- **Encore's threads are always joined, never killed.**
  - `BackgroundJobs` is a small pool of its own (`std::thread`, joined).
    Jobs still waiting are dropped; a running job finishes. Plugin creation
    gives up within 50 ms once its show is closed.
  - Every `juce::Thread` stops through `stopThreadWithoutKilling`
    (`ThreadStop.h`). It waits as long as it takes and logs once after 5 s.
  - A test fails on any `stopThread`, `killThread` or `juce::ThreadPool` in
    `Source/`.
- **The tests have a hang watchdog** (`Tests/HangWatchdog`):
  - With `ENCORE_TEST_HANG_SECONDS` set (CI: 180 s), a test process with no
    progress for that long prints the step it was in and exits with code 3.
    Progress is each test step, plus a heartbeat from the soak; the longest
    step takes about 20 s. It's off by default, so a test paused in a
    debugger isn't killed.
  - On Windows it also prints every thread's stack. It does so without the
    loader, the heap or the console, which a hang may be holding: frames as
    module + offset first, then names from a helper thread that gets 30 s.
  - EncoreTests links DbgHelp and gets a PDB in Release (`/DEBUG`, with
    `/OPT:REF` and `/OPT:ICF` kept on). A failed CI job uploads the exe and
    the PDB.

## 0061 — Focus rings: who moved focus decides, not timing

Decision 0059's rings still showed after a click (Adrian, 20 Nov 2026): picking
an amp template rebuilt the inspector, focus landed on its first button
("British gold") more than 300 ms after the click, and the rule ("focus that
moves within 300 ms of a click came from the click") took it for the keyboard.
The ring was also drawn 2 px outside the button, beyond what the button's
paint can reach, so only its four rounded corners showed.

- **Rings show as CSS `:focus-visible` does, without a clock:**
  - a click hides them, and the pointer is in charge;
  - focus moved while a key is held (Tab, an arrow, Return, Space, Esc, F1–F12)
    shows them, and the keyboard is in charge;
  - focus set by code (a rebuilt panel, a dialog's default button) keeps
    whoever is in charge.
  The token `state::focusFromPointerMs` is gone.
- **`paint::focusRing` takes the room its caller can paint in.** The ring
  goes 2 px outside the control when it fits, or on the control's own edge
  when it doesn't (buttons, pane tabs, fields). It's always drawn whole.

## 0062 — Native modules: shared base; choices, hidden types, schema 5 for new modules

Encore's own effects ("native modules"; players will see them as **FX
Units**, their DSP branded the **Encore Engine**) are `Block`s in our own
C++, never VST3. Stage B0 of their build plan adds what every module needs
and nothing players see (Adrian, 26–27 Nov 2026).

- **Parameter choices.** `Parameter::Spec` can carry named steps
  (`Spec::choice`): the value is an index 0..n−1, saved by index, shown by
  name, clamped and snapped. A choice never ramps.
- **A choice change is a true crossfade** (Adrian: "I prefer the true
  crossfade"): `dsp::Crossfade` lets a module run its old setting beside the
  new one for 10 ms and blend them linearly (the two paths are correlated).
  Only the module can run both, so it's a helper modules use, not something
  `Block` does for every parameter. A change during a fade waits for it.
- **Hidden block types.** `BlockType::hidden` keeps a type out of the Add
  block menu (`getMenuBlockTypes()`); `createBlock`, shows, the tests and
  EncoreRender still make it. Every FX Unit stays hidden until its UI is done
  and Adrian's rig test has passed; main stays show-safe.
- **The engine's test unit** (`engineProbe`, hidden, Adrian's pick): one
  filter band, a drive with the Quality switch, a trim. It lets the tests,
  EncoreRender and the benchmark run a real block before the first FX Unit.
  Bit-exact at its defaults.
- **Schema 5.** `BlockType::newModule` marks a module an older Encore
  doesn't know (it would skip the block and play the show without it). A
  show that uses one is saved as schema 5; any other show keeps the version
  it needs (as 0047, 0051 and the amp builder did), and an older Encore
  refuses schema 5 with its "newer version of Encore" message. The model
  asks `isNewModuleBlockType()`, declared in `ShowModel.h` and defined with
  the registry, so it keeps no list of its own.
- **Shared DSP in `Source/engine/dsp/`** (written from papers and maths;
  coefficients and states in double, buffers float; see its README):
  - `FilterDesign`: decramped bells and shelves (impulse-invariant poles,
    the magnitude matched to the analog prototype at DC, the centre and
    Nyquist, after Vicanek 2016; a cut is the reciprocal of its boost and a
    high shelf is G over the low shelf, so every case is stable and minimum
    phase); bilinear sections; Butterworth cuts 6–96 dB/oct.
  - `Svf`: the TPT state-variable filter (after Zavalishin and Simper),
    which plays any of those sections exactly and stays stable under fast
    modulation.
  - `FilterBand`: a band as up to 8 sections, and **one response function**
    computed from the numbers the audio plays, so a curve on screen is what
    you hear.
  - `Detector`: Peak and RMS detectors with a detector time, and a gate
    envelope with attack, hold, release and hysteresis, sample-accurate.
  - `Oversampler`: the Quality switch (Live 2×, High 4×, Max 8×), all three
    prepared up front so switching allocates nothing, crossfaded; polyphase
    IIR with integer latency (Adrian: lowest latency for live use), reported
    per Quality.
- **Measured, with stated tolerances,** at 44.1, 48, 96 and 192 kHz on
  generated, seeded signals only (sweeps, noise, plucks, palm-mutes, a
  decaying sine): measured = computed response; bells and shelves within
  0.45 dB of the analog curve to fs/16 at up to 15 dB, 0.9 dB to fs/4, up to
  3.5 dB only for 30 dB right at the top; aliasing of a hard tanh −24 / −36 /
  −57 dB at 2× / 4× / 8× (−15 dB without); no allocation; no NaN.
- **EncoreRender** (`Tools/EncoreRender`, core in `session/OfflineRender`):
  a WAV through one block or a patch, settings as JSON, a 32-bit float WAV
  and a report (peak, RMS, latency, CPU per block). ctest renders a
  generated sweep; CI prints the report and uploads the renders.
- **A CPU benchmark** in EncoreTests (48 kHz, 64 samples, fixed settings):
  a report per block and DSP piece, not a failing test; module stages
  compare it with their plan's targets.

## 0063 — Touch screens: hold is a hold; a two-finger tap is a right click

On a Windows touch screen a finger held still turned into a right click
(Windows' "press and hold"), so holding or dragging only worked after the
finger moved a little (Adrian, 20 Nov 2026).

- **Press and hold off:** the main window sets the documented
  `MicrosoftTabletPenServiceProperty` flags (press and hold, pen feedback and
  flicks off). It sets them when it opens and whenever it's activated.
- **A two-finger tap is a right click**, everywhere except the live Stage
  (Adrian's choice), where widgets act the moment a finger lands.
  - It starts when a second finger lands within 300 ms of the first, before
    the first has moved. It counts as a tap when both are lifted within 700 ms
    and neither moved more than 24 px.
  - The first finger's press is cancelled, so nothing under it clicks. The
    second finger never reaches the app.
  - The right click goes where the first finger was.
  - The rules are in `TwoFingerTapDetector`, with unit tests.
- **Inside JUCE, not around it:** the first build hooked its window from
  outside (`SetWindowSubclass`), and Chrome flagged that download as
  dangerous; the build without it wasn't flagged. So JUCE's own window code
  now hands touch messages to Encore first: a small edit made on the
  fetched JUCE source by `cmake/PatchJuceTouch.cmake`.
  - The script runs on every configure, but edits the file only once.
  - It never edits a local JUCE checkout (`ENCORE_JUCE_DIR`).
  - If the code doesn't match (a new JUCE version), it warns and the app
    builds without the two-finger tap.
- **JUCE reads the touches itself** (`setUsingWindowsMultiTouch`, one input
  source per finger) on the main window. Left to Windows, a touch became a
  mouse click only when the finger lifted; on a two-finger tap that late click
  arrived after the right click and closed the menu it had opened (Adrian, 20
  Nov 2026, on a block in the signal chain). Now a press lands the moment the
  finger does. Windows' built-in gestures (pinch) stop; Encore uses none.
- **Tested on a real window:** a test opens a menu with the synthetic right
  click, and with a two-finger tap made of injected touches
  (`InjectTouchInput`, no touch screen needed), and checks it stays open. On CI
  a touch that can't be injected fails the test; elsewhere it's skipped.
- **Code signing** is still the real answer to download warnings ("Production
  basics" in the roadmap).

## 0064 — Menus: the design's shadow around the rounded shape, and text 10 px in

*20 Nov 2026.* Adrian saw a "triangle" under each rounded corner of a menu, as
if a square menu were drawn under the rounded one.

- **Why:** JUCE draws a window's drop shadow for a square window
  (`DropShadower`). Around a rounded menu its square edge showed, and the corner
  the rounding leaves out stayed unshadowed. A test that reads a real menu off
  the screen on Windows saw both: clear corners, a square shadow outside.
- **Now:** menus ask JUCE for no shadow (`getMenuWindowFlags` = 0).
  `preparePopupMenuWindow` gives each menu window a `theme::ShapedShadow`: one
  click-through window right behind it with the design's shadow, `0 12 30
  rgba(0,0,0,.6)`, drawn as CSS draws `box-shadow`. That means the rounded shape
  moved 12 px down, blurred by a Gaussian of half the blur (15 px), and left out
  inside the menu. Its image is made once per size and screen scale, never per
  frame. It follows the menu (moves, hides, goes with it), is placed like JUCE's
  own shadow windows (screen scale, second screens), and sits behind the menu
  as a sibling when a menu lives inside a component.
- **The shadow window paints with JUCE's software renderer** and paints the
  moment it is shown. On CI, Direct2D drew fills into a semi-transparent window
  but no image at all (a software image or a native one), so a Direct2D shadow
  window stayed empty; and Windows sends no paint message to such a window, so
  waiting for one left the shadow empty now and then. The image is cached, so
  the software renderer costs one copy when the menu opens.
- **Text 10 px in, as the v3.2 boards draw it** (items padded `0 10`). A menu
  with a check mark keeps the 14 px tick column (text 24 px in, headers 20).
  A menu window finds out for itself, as it opens, whether any item is ticked.
  Unknown means room for a tick, so a tick never covers text.
- **Separators stay #3A3A3F:** the boards draw that; the §6 text says #34343A.
- **Tests:** the rendered shadow (clear inside, round at the corners, the same
  at 1×, 1.5× and 2×). On Windows, a real menu over a magenta window, read back
  from the screen at 100 % and 150 %: the corners show the window behind them,
  darkened by the shadow under the bottom ones, and the menu has its shadow
  window. Menu snapshots draw the same shadow.
- **Left as it is:** other shadows (dialogs, fault cards) pass the design's
  blur to `juce::DropShadow`. Its radius is not a CSS blur (about √radius
  standard deviation), so those shadows are tighter than the boards. They
  could move to the same Gaussian later.

## 0065 — Pedal EQ engine: proportional Q, the 9 V soft clip, factory preset format, capture slot

The Pedal EQ "G7" (`pedalEq`) is the first FX Unit: stage B1 of the native
modules (EQ-PLAN §3). Its sound, parameters, presets and show format are
built; it stays **hidden** (no UI until U1, then Adrian's rig test) and is a
new module, so a show that uses it saves as schema 5 (0062).

- **Signal:** Input (Pad −6 dB / Normal / Boost +6 dB) → character capture →
  seven decramped bells at 100 Hz … 6.4 kHz → the 9 V soft clip → Level
  (±15 dB). It keeps the channels it gets; no tail.
- **Proportional Q** (Classic): Q = 1.07 + 1.80 · |gain| / 15, so about
  1.3 octaves near 0 dB and 0.5 octave at ±15 dB, following the ramped
  gain, as an analog gyrator graphic EQ does; Wide is a constant 1.07.
  Measured: the bandwidth at ±15 dB is Q 2.87 / 1.07 within 3 %.
- **Smooth sliders, a shared change:** redesigning every 16 samples left
  zipper noise at −53 dB on a 30 dB sweep in 50 ms. `dsp::FilterBand::glideTo`
  now designs for the end of each 16-sample chunk and moves the filter's
  natural parameters (g, k and the mix) every sample: −88 dB, as good as
  redesigning every sample, at a fraction of the cost.
- **The 9 V soft clip** (`dsp/SoftClip.h`, shared with the drives to come):
  exactly linear to −6 dBFS, then a tanh shoulder that joins the line without
  a kink; 1 dB down at +6 dBFS (positive) and +5 dBFS (negative), so the
  slight asymmetry adds a little even-order warmth when it's pushed (values
  Proposed, tuned on the rig in U1). Oversampled by Quality, then a 1 Hz DC
  blocker. THD below −80 dB at −12 dBFS with Boost; slammed, aliasing
  −24.5 / −39 / −69 dB at Live / High / Max.
- **When the clip is in the path** (Adrian, 27 Nov, `realClip`): Off
  (default): only when the settings can push it (a slider above 0 dB, or
  Boost), so Flat stays bit-exact with no latency even for a hot input. On:
  always, so a hot input from an earlier block clips like a real pedal.
  Coming in or out crossfades 30 ms and the reported latency follows (split
  paths align, 0053); the stage enters settled, from the last 128 samples.
- **`dsp::Crossfade` is a smoothstep, a shared change:** still equal-amplitude,
  but without the corners of a straight ramp, which sprayed sidebands to
  −58 dB when fading between paths a few samples apart.
- **HOT:** a lock-free flag, on while the clip takes off more than 1 dB and
  for 300 ms after (Proposed); U1 shows it next to Level.
- **Factory presets, the shared format** (MODULES-PLAN §3.1): compiled-in
  tables of { stable id, name, parameter values }. A preset sets parameters
  only; every parameter the block counts as part of a preset goes to its
  default, then to the preset's values, so a preset is the whole sound and
  one undo step later. The Pedal EQ's six (EQ-PLAN §4, Proposed): flat,
  midPush, scoop, cleanBoost, bright, thicken. They set the face (sliders,
  Level, Q, Input), not the setup (Quality, Capture level, Real pedal
  clipping). EncoreRender renders a preset by id. User presets, the menu
  and A/B come with U1.
- **The character capture** (MODULES-PLAN §4): "Load capture…", a NAM capture
  of a real pedal's buffer, after Input and before the bands. It reuses the
  amp's NAM path (background load and prewarm, lock-free handover, 30 ms
  crossfade, freed on the message thread, copied into the show's models
  folder). One copy per side (Adrian: for now; a setting later).
  `captureLevel` (Adrian: a per-pedal setting, not on the face): As captured
  (default) or Level-matched (a gain measured once with noise at −18 dBFS RMS).
  A missing file plays "DSP only" and the info says it's not on this computer.
- **CPU:** every slider at ±10, Classic, Live, the clip in the path: within
  the plan's 0.5 % target at 48 kHz / 64 (the CI benchmark reports it).

## 0066 — Gates engine: S2 and X4, key sources and taps, linking, the adaptive gate, look-ahead

Stage B2 of the native modules (GATE-PLAN): the engine of the **Pedal Gate
"S2"** (`pedalGate`) and the **Studio Gate "X4"** (`studioGate` for now).
Both are FX Units, **hidden** and new modules (a show with either saves as
schema 5, 0062), with no UI until U2. **Today's gate (`gate`) is unchanged**
and keeps playing old shows until U2 switches it over.

- **One gate engine** (`GateBlock`) under both: key → key filter → detector →
  the mode's logic → gain on the block's own channels. A change of mode,
  detector, key, stereo or link crossfades 10 ms, the new engine starting
  where the old one stood (a closed gate never bursts open). Both start open.
- **The S2:** Threshold (3 dB per step above the learned floor, 0 to +30 dB),
  Decay (20 · 40^(knob/10) ms), Detect (Input / Guitar), Mode (Hard −90 dB /
  Soft, an expander 1:2 down to −15 dB); fixed Peak 10 ms, hysteresis 6 dB,
  hold 20 ms, attack 0.3 ms, stereo linked. Presets cleanHiss, singleCoils,
  highGain, djentTight.
- **The X4:** Mode Gate / Expander / Ducker / Smart; Threshold, and a separate
  Threshold (Smart) offset so a mode change never reinterprets a value; Smart
  curve Gate / Expander (Adrian, 27 Nov: how Smart mode plays the S2's Soft);
  Range, Ratio, Attack, Hold, Release, Hysteresis, Detector, Detector time;
  a key filter (12 dB/oct, the knob ends read Off); Key (Input, Guitar, an
  interface input, an earlier block); Stereo linked or independent;
  Look-ahead 0.5–5 ms. Defaults are today's gate's values. Presets: the S2's
  four in Smart mode (measured the same as the S2 within 1e-6), duckDelay,
  duckReverb, softCleanup, swellCleanup. Stereo and Look-ahead are the setup,
  not part of a preset; refs never are.
- **Shared gate DSP** (`dsp/`): `GateEnvelope` gains the expander
  ((T − L)(r − 1) dB, at most |Range|; its open flag, for the OPEN light and
  a follower, keeps the gate's hysteresis and hold so it doesn't flicker on
  noise at the threshold), the ducker and a decision made elsewhere (for a
  follower); `LookAhead` (a delay whose time changes
  crossfade, on `FadingDelay`); `AdaptiveGate`, the "Smart" logic both use:
  the floor learns from 500 ms stretches of steady noise with no pick attack,
  6 dB/s at most, −96 to −40 dBFS, from −70; it freezes below −85 dBFS and
  while the tuner is open (any tuner, Adrian); the release follows the note's
  decay (0.25× to 2.5×); a reopen within 50 ms doubles the hold (to 200 ms);
  it opens with a 0.3 ms fade. All Proposed, as named constants, tuned on the
  rig in U2.
- **References by name** (`BlockModel::refs`, schema 5): `keyInput` (an
  interface input by name), `keyBlock` (an earlier block by id), `linkTo` (a
  leader gate by id). Part of the block, never of a preset. A reference that
  doesn't resolve never stops the patch: the gate keys from its own input (or
  decides on its own) and its info says why.
- **Key sources in the engine:** `ProcessContext` carries the interface's
  inputs (from `PatchPlayer`'s copies), whether the tuner is open, the
  callback count and the latency at the block's input; every other block
  ignores them. A block keyed from an earlier one makes that block a **tap**:
  the `Chain` copies its output into a buffer sized in `prepare`; a block
  nobody keys from costs nothing. **"Earlier"** (decision 10): the main lane
  before the split is before everything after it; each path counts only its
  own earlier blocks (path B doesn't count path A: it runs first but is
  parallel; Claude, Adrian left it to us); after the merge, everything before
  it. Tails only: a skipped block's tap is silence.
- **Linking** (GATE-PLAN §4): a follower shares its leader's level,
  open/closed decision and threshold, and keeps its own Range and Release or
  Decay. The leader writes a `LinkBus` sized in `prepare`; the follower reads
  it in the same callback, delayed by any latency between them. A follower's
  own look-ahead is ignored while it follows. Leader off, asleep or skipped:
  its own detector with the last shared threshold ("Link: leader off"), and
  the link back, crossfaded, when the leader runs again. Removing a block
  drops the links and keys that pointed at it, in the same edit.
- **Split paths follow a latency that changes while playing** (a shared
  change): the `Chain` re-reads both paths' latency every buffer and moves its
  alignment with a 30 ms crossfade, so a look-ahead or the Pedal EQ's clip
  coming in keeps the paths lined up (0053).
- **Kept per patch:** a rebuilt chain's gates start from the floor the
  playing one learned (by block id, on the message thread). Not saved to disk:
  a fresh show learns again within seconds.
- **The Gate pair**, model side: an S2 before the first amp (Soft, Threshold
  3) and one after it (Hard, Detect Guitar) linked to it, one edit; refused
  without an amp. The menu entry is U2's.
- **Today's gate as an X4**, written and tested, not switched on: Threshold and
  Range copy over; Detect guitar becomes Key; **Release is converted**
  (Adrian, 27 Nov): today's is a one-pole time constant, falling 8.7 dB per
  release time, while the X4 falls the whole Range in its release time, so
  the X4 gets release · |Range| / 8.7 dB (capped at 2000 ms). Measured: the
  same gain curve within 0.2 dB above −60 dB after the first 2 ms. A new X4
  keeps 80 ms (it closes fully in 80 ms).
- **The display feed:** while a gate's display is on, the audio thread pushes a
  frame about every 5 ms (input, key and output peaks, gain reduction, open)
  into a lock-free FIFO; off, it costs nothing. U2 draws the 4 s history.
- **EncoreRender:** both types and their presets; `--ref` for references
  (`blockId.name=value` with `--show`); report notes with each gate's opens,
  learned floor, key and link; `--make-gate-test` (notes with gaps of noise)
  and `--make-gate-show` (the pair around an amp, a ducking delay, a Smart X4).
- **CPU** at 48 kHz / 64 (GATE-PLAN §7): the S2 against ≤ 0.2 %, the X4 with
  its key filter and 1 ms look-ahead against ≤ 0.4 %; the CI benchmark
  reports both. The benchmark now runs on its own (`EncoreTests.benchmark`,
  RUN_SERIAL): beside the soak, the screens and the renders it measured the
  machine's load.

## 0067 — Studio EQ core engine: 24 slots, shapes and slopes, output tools, auto gain, A/B data

Stage B3 of the native modules (EQ-PLAN §2): the core of the **Studio EQ
"Q24"** (`studioEq`), an FX Unit, **hidden** and a new module (schema 5),
with no UI until U3. **Today's EQ (`eq`) is unchanged** and keeps playing old
shows until U3 switches it over. Dynamics, sidechain, placement, character and
Quality are B4; linear phase and spectral bands B5; Natural phase R1.

- **A slope on every shape, as in Pro-Q** (Adrian, 27 Nov): the number is the
  filter's order × 6 dB/oct (a 12 dB/oct band pass is second order, so each
  skirt falls 6 dB/oct). Shelves: 6 = first order, matched at DC, the
  half-gain point and Nyquist; 12 = the B0 shelf, Q shapes the corner;
  18–96 = Butterworth shelves made of matched second-order shelves at the same
  frequency (decramped, Q has no effect). A new **tilt shelf** (the high shelf
  less half its gain). A **bell has no slope** (Adrian, 27 Nov). Notch and band
  pass cascade a section per pole pair (the band pass keeps the −3 dB width Q
  sets); all pass turns −90° per 6 dB/oct. Brickwall plays as 96 until B5.
  Measured against the prototypes (dsp README): within 0.03 dB for a corner up
  to fs/48 (6 dB/oct 0.11), 0.15 dB up to fs/10 at ±12 dB, 2.35 dB at worst
  (a steep shelf at ±30 dB set above ~8 kHz at 44.1/48 kHz).
- **24 slots, 373 parameters, ids final**: per slot `bNNUsed`, `On`, `Shape`,
  `Freq`, `Gain`, `Q`, `Slope` and B4's `Place`, `Mode`, `Range`, `Thresh`
  (−61 = Auto), `Attack`, `Release`, `Side`, `SideFilter`; block-wide `phase`,
  `linearQuality`, `character`, `drive`, `quality`, `output`, `pan`, `width`,
  `autoGain`, `gainScale`, `invert`, `cpuTight`, `range`. Later stages' are
  saved now and play nothing yet. A parameter can be kept off expression
  pedals (`Parameter::Spec::expressionAllowed`, a shared addition): a band's
  in use, placement, mode and sidechain, the phase mode, Quality, the CPU
  setting, the display range.
- **The bands** play in slot order, in series, in double. A free slot, a band
  that's off or one that changes nothing costs nothing and passes bit-exact.
  On, off, in or out of use glides to or from flat (10 ms); a Shape or Slope
  change crossfades that band alone. Moving values redesign every 16 samples
  with `glideTo`; **Freq and Q ramp in log steps**, as a knob turns (the
  shared parameter ramp is linear, which jumped octaves per chunk near 10 Hz).
  Measured zipper noise −75 dB or lower (−43 without the glide).
- **Bands come and go as model edits** (`addEqBand`, `removeEqBand`; `ShowSession`):
  the lowest free slot, "All 24 bands are in use"; removing a band resets its
  slot and drops its expression assignments in the same edit. Heard at once,
  no rebuild (only a dropped assignment rebuilds). The code has no undo yet;
  the undo round trip is tested when it lands.
- **Output tools**, in order bands → auto gain → output → pan / width (stereo
  only) → invert. Pan and width do nothing on mono (Adrian, 27 Nov: greyed in
  U3). **Auto gain is Off · Curve · Smart** (Claude; Adrian asked for a smart
  and a normal one): Curve keeps pink noise from 80 Hz to 8 kHz at the same
  RMS, computed from the designs at 48 points, only while a band moves; Smart
  starts there and corrects by K-weighted loudness (BS.1770, a new shared
  `dsp/Loudness.h`) in minus out over 3 s, only while someone is playing and
  the tuner is closed, at most 1 dB/s, within ±6 dB of Curve. On very
  bass-heavy material a big treble move can need more than 6 dB from Curve;
  Smart then stops at the limit (values Proposed, tuned on the rig in U3).
- **Factory presets** (static ones): flat, tightenLows, cutMud, presence,
  sitInMix; Tame fizz and Pick attack wait for dynamic bands (B4). A preset is
  the whole sound; the setup (pan, width, invert, display range, Quality, CPU
  setting, phase) isn't part of one.
- **A / B, for every FX Unit** (MODULES-PLAN §3.1): `BlockModel::Compare`, the
  other set of parameter values and which one plays, saved only once used,
  read only for FX Units. Switch (B starts as a copy of A) and copy either
  way, one edit each, heard at once; a capture keeps it.
- **Today's EQ as a Studio EQ** (`convertOldEq`), written and tested, not
  switched on: the five sections into slots 01, 02 … (the shelves at RBJ's
  S = 1, Q 0.7071), on/off, stomp and pedals kept, a band also for a control at
  its off value that's on a pedal. Within 0.21 dB of today's EQ at everyday
  settings, 0.67 dB at worst (every knob at an extreme: today's bilinear bell
  is cramped at 5 kHz at 44.1 kHz, the Studio EQ's follows the analog curve).
- **Show size:** 8 Studio EQs add about 100 KiB with every value written and
  parse in a few ms: kept as is (writing only what differs from the default
  is an option for later).
- **CPU** at 48 kHz / 64: 8 static bands, stereo, against the 1 % target; the
  CI benchmark reports it with Smart's share and 24 bands.

## 0068 — Studio EQ dynamics: dynamic bands and sidechain, stereo placement, character, When CPU is tight

Stage B4 of the native modules (EQ-PLAN §2.1, §2.2, §2.4, §2.6): the Studio
EQ's parameters that B3 saved now play. It stays **hidden** (schema 5) until
U3; today's `eq` is unchanged. Linear phase, Brickwall and real spectral
bands are B5; Natural phase R1. Values marked Proposed are named constants,
tuned on the rig in U3.

- **Two new parameters per band, after B3's** (so no id moves; 421 in all):
  `bNNSideFreq` (20 Hz–20 kHz, log, 1 kHz) and `bNNSideQ` (0.1–10, log, 1.0),
  the sidechain filter's own frequency and Q when `SideFilter` is Own.
  Expression allowed; preset parameters. A B3 show without them loads the
  defaults.
- **Dynamic bands:** a Bell, shelf or Tilt in Mode Dynamic moves its gain from
  its Gain towards Range while its sidechain is over the threshold: 1 dB per
  dB over, a 6 dB soft knee, up to |Range|; Attack and Release smooth the
  movement in dB (time constants). On the other shapes Mode is ignored.
  - **Sidechain:** This band = the EQ's input (before any band: bands don't
    steer each other); Dry guitar = `ProcessContext::chainInput`, This band
    when there's none. Filter: Follows the band (a Bell: band pass at its
    frequency and Q; low shelf: 12 dB/oct low pass; high shelf: 12 dB/oct high
    pass; Tilt: none) or Own (band pass at SideFreq, SideQ). RMS detector,
    5 ms (Proposed).
  - **Auto threshold:** a 3 s RMS of the filtered sidechain plus 3 dB, learning
    only while someone plays (the source above −50 dBFS over 50 ms, and the
    16 samples now not silent) and the tuner is closed; frozen otherwise; no
    movement before it has heard anything (Proposed).
  - **Gain scale scales the movement** as it scales the Gain (Adrian, 28 Nov).
    **Auto gain's Curve follows the set Gain, not the movement** (Adrian,
    28 Nov), so it doesn't push the level back up each time a band acts. Smart
    measures the real output, so it sees a band's average dip, at most 1 dB/s.
  - A moving band redesigns every 16 samples with `glideTo` (zipper noise
    −94 dB above 6 kHz; −54 without the glide) and costs as a static band
    while it rests; Range 0 at rest is bit-exact static.
  - **Spectral plays as Dynamic**, bit for bit (`playedMode`), until B5 swaps
    in its own path; the parameters stay.
  - For U3, lock-free: `getLiveGainDb`, `getDynamicGainDb`, `getAutoThresholdDb`.
- **Stereo placement** (decision 8): Left and Right filter one channel (the
  other bit-exact); Mid and Side encode M = (L + R) / 2, S = (L − R) / 2 around
  that band, filter one, decode (the other to float precision). A band that
  changes nothing makes no M/S round trip. On mono, Place is ignored. A Place
  change crossfades that band only. A dynamic band's detector follows its
  placement (Stereo: the louder channel); from the dry guitar every placement
  hears the guitar.
- **Character** (decision 10), after the bands, before auto gain. Clean is not
  in the path (bit-exact, no latency). Soft and Warm are new shared curves
  (`dsp/Saturation.h`): Soft symmetric, odd-order only (linear to −12 dBFS,
  1 dB down at 0 dBFS); Warm (1 − e^(−0.3x)) / 0.3 into an asymmetric SoftClip
  inside a +3 dB, 150 Hz low emphasis and its exact inverse, so its second
  harmonic leads up to a firm drive. Drive 0–24 dB in, the same off after
  (a −30 dBFS sine within 0.5 dB at every Drive); a 1 Hz DC blocker. Quality
  Live / High / Max oversamples 2× / 4× / 8× (latency 4 / 6 / 6 samples at
  48 kHz, reported, so split paths follow it). In or out of the path, and Soft
  ↔ Warm, crossfade 30 ms on a second path warmed up from the recent input; a
  Quality change crossfades 10 ms. `Oversampler::resetTo` (shared) starts at
  a saved Quality without a fade, and the Oversampler now keeps a mono set
  of its own: JUCE's integer-latency delay line asserted on a mono block
  through the stereo set, and in Debug the assertion allocated on the audio
  thread (the Pedal EQ on mono did it too).
- **When CPU is tight** (decision 12): the shared rule, in the engine
  (`CpuTightRule`): the callback over 85 % of its time for 8 buffers in a
  row, or an overrun. `AudioEngine` times each callback once, for the load
  meter and the rule, and passes it to every block in the next one
  (`ProcessContext::cpuTight`). With Make it lighter the Studio EQ takes one
  step per tight stretch, then waits 1 s: Spectral as Dynamic, Linear phase
  as Zero latency (both nothing until B5), Quality to Live. **Only a step that
  changes something now is taken** (Adrian, 28 Nov): with nothing left, the
  block isn't marked lighter. It stays lighter until `backToFull()`, isn't
  saved, never switches the EQ off. Keep the sound changes nothing.
- **Band listen, engine side:** `setListen` / `clearListen`, any thread; not a
  parameter, not saved. The output becomes the EQ's input through a band pass
  at the band's frequency and Q (shelves and cuts: the side they act on), on
  every channel, crossfaded 20 ms; a dynamic band plays its sidechain as its
  detector hears it.
- **Presets** `tameFizz` and `pickAttack` (EQ-PLAN §4 values, Proposed).
- **EncoreRender:** `--make-stereo`, `--make-eq-show`; ctest renders of both
  presets, each placement on stereo, Soft and Warm at Drive 0 / 12 / 24 at
  each Quality, and a dynamic bell after the NAM example amp keyed from the
  dry guitar or its own band.
- **Threads:** every new processing function runs on the audio thread
  (no allocation, no locks: everything is sized in `prepare` for 24 dynamic
  bands in stereo, the sidechain filters, the M/S buffers and every Quality).
  The getters, `setListen`, `clearListen` and `backToFull` are any-thread
  atomics; `CallbackTimer` runs at the end of the audio callback.
- **CPU** (Windows CI, 48 kHz / 64, average): one dynamic bell (Auto, moving
  all the time) adds **0.14 %** over the same bell static (target 0.3 %);
  **Warm at High, stereo 0.93 %** (target 1.5 %; mono 0.52 %); 8 bands with 4
  dynamic, Mid/Side and Soft at Live, stereo, 0.93 %. B3's 8 static bands,
  stereo, 0.55 %.
