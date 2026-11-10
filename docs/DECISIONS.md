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
