# Source/engine/dsp: the Encore Engine's DSP

Building blocks for Encore's native modules (FX Units) and the engine
(decision 0062). Plain C++ on JUCE; no file here allocates, locks or does I/O
on the audio thread (see each header for which thread each function is for).
Coefficients and filter states are double; audio buffers are float. Written
from papers and maths in our own words, never copied.

| File | What |
|---|---|
| `FilterDesign` | Second-order sections: decramped (matched) bells and shelves after Vicanek (2016), bilinear low/high/band pass, notch, all pass, first order; Butterworth Qs; the analog prototypes' magnitudes |
| `Svf.h` | The TPT state-variable filter (Zavalishin, Simper); plays any stable section exactly and stays stable under fast modulation |
| `FilterBand` | A band (bell, shelves, cuts 6–96 dB/oct, notch, band pass, all pass) as up to 8 sections, and the one response function the audio and the UI share; `glideTo` moves a band to a new design sample by sample (no zipper noise when a slider or pedal moves) |
| `Detector.h` | Peak / RMS level detectors and a gate envelope (attack, hold, release, hysteresis) with the X4's modes: Gate, Expander ((T − L)(r − 1) dB, at most \|Range\|), Ducker, and a decision made elsewhere (a linked follower) (decision 0066) |
| `AdaptiveGate.h` | The "Smart" gate the S2 and the X4's Smart mode share: the noise floor learned from steady noise (frozen near silence and while tuning), a release that follows the note, a chatter guard, a 0.3 ms opening fade; its values Proposed, as named constants (decision 0066) |
| `LookAhead.h`, `FadingDelay.h` | A delay for the audio path (the gates' look-ahead, up to 5 ms at 192 kHz; the Chain's split alignment) whose time changes crossfade instead of jumping |
| `Oversampler` | The Quality switch: 2× / 4× / 8×, prepared up front, crossfaded, latency reported |
| `Crossfade.h` | The 10 ms true crossfade for a choice change (old and new setting side by side), equal-amplitude with a smoothstep shape |
| `SoftClip.h` | A smooth, slightly asymmetric soft clipper (a 9 V op-amp stage into its rails): linear to −6 dBFS, 1 dB down at +6 / +5 dBFS; the Pedal EQ's clip, the drives later (decision 0065) |
| `Biquad.h`, `OutputEq.h`, `SafetyLimiter`, `PitchDetector`, `StreamResampler` | Older engine pieces (today's EQ, output EQ, limiter, tuner, resampling) |

Tests: `Tests/ModuleDspTests.cpp` (every rate from 44.1 to 192 kHz, generated
signals from `Tests/TestSignals.h`), the CPU benchmark in
`Tests/BenchmarkTests.cpp`, and `EncoreRender` for listening.
