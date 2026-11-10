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
through cues) doesn't ring out at all. Up to four patches can ring at once; a
fifth stops the oldest, which is the quietest. Going back to a patch that's
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
