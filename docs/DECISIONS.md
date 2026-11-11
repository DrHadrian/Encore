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
file gets a line in plugin-scan.log next to the plugin list.

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
- the crash handler records which plugin the crashing thread was inside; after
  such a crash that block starts switched off, with a notice, until turned back
  on.
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
