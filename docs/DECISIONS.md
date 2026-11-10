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
show it. A proper look-ahead-free safety limiter replaces the clamp in
milestone 1.

## 0006 — Machine settings stay out of shows (milestone 0)

Device, buffer, input and volume live in `%APPDATA%\Encore\Encore.settings`.
Show folders (milestone 4) hold only what belongs to the show, so the same show
runs on any computer.

## 0007 — One instance at a time (milestone 0)

Most ASIO drivers handle two clients badly, so a second launch just brings the
running Encore to the front.
